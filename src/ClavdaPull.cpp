#include "ClavdaPull.h"

#include <HTTPClient.h>
#include <Update.h>
#include <WiFiClientSecure.h>
#include "ClavdaOTA.h"

namespace clavda {

// No bytes for this long counts as a stalled download.
static const uint32_t STALL_MS = 20000;
// Longest a single loop() spends writing flash before handing control back.
static const uint32_t SLICE_MS = 50;

Pull::~Pull() {
  release();
}

bool Pull::start(const Job &job, const char *caRoots) {
  if (busy()) return false;
  release();
  _job = job;
  _caRoots = caRoots;
  _written = 0;
  _error = "";
  _flashOpen = false;
  _lastData = millis();
  _phase = CONNECTING;
  return true;
}

void Pull::reset() {
  if (busy()) return;
  release();
  _phase = IDLE;
  _written = 0;
  _error = "";
}

void Pull::loop() {
  switch (_phase) {
    case CONNECTING:
      open();
      break;
    case DOWNLOADING:
      stream();
      break;
    case VERIFYING:
      verify();
      break;
    default:
      break;
  }
}

void Pull::abort(const char *why) {
  if (!busy()) return;
  fail(why);
}

void Pull::open() {
  const bool https = _job.url.startsWith("https://");
  if (https) {
    WiFiClientSecure *tls = new WiFiClientSecure();
    // Certificates are always checked: the bundle's roots cover the download host.
    tls->setCACert(_caRoots);
    _client = tls;
  } else {
    _client = new WiFiClient();
  }

  _http = new HTTPClient();
  if (!_http->begin(*_client, _job.url)) {
    fail("Could not open the download address");
    return;
  }
  _http->setTimeout(STALL_MS);
  _http->setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  _http->setUserAgent("ClavdaOTA/" CLAVDA_LIB_VERSION);

  const int code = _http->GET();
  if (code != HTTP_CODE_OK) {
    // 403 is what S3 answers once the presigned link has expired.
    fail(code > 0 ? String("Download failed (HTTP ") + code + ")"
                  : String("Could not reach the download host: ") + HTTPClient::errorToString(code));
    return;
  }

  const int length = _http->getSize();
  if (length < 0 || (size_t)length != _job.size) {
    fail(String("Download is ") + length + " bytes, the job expects " + _job.size);
    return;
  }

  if (!Update.begin(_job.size, U_FLASH)) {
    fail(String("Cannot start the update: ") + Update.errorString());
    return;
  }
  _flashOpen = true;
  _sha.begin();
  _lastData = millis();
  _phase = DOWNLOADING;
}

void Pull::stream() {
  WiFiClient *body = _http ? _http->getStreamPtr() : nullptr;
  if (!body) {
    fail("The download connection went away");
    return;
  }

  uint8_t buf[1024];
  const uint32_t sliceEnd = millis() + SLICE_MS;
  while ((int32_t)(millis() - sliceEnd) < 0) {
    if (_written >= _job.size) {
      _phase = VERIFYING;
      return;
    }

    const int avail = body->available();
    if (avail <= 0) {
      if (!body->connected()) {
        fail(String("The download ended early at ") + _written + " of " + _job.size + " bytes");
      } else if (millis() - _lastData > STALL_MS) {
        fail("The download stalled");
      }
      return;
    }

    size_t want = (size_t)avail < sizeof(buf) ? (size_t)avail : sizeof(buf);
    if (want > _job.size - _written) want = _job.size - _written;
    const int n = body->read(buf, want);
    if (n <= 0) return;

    _lastData = millis();
    if (Update.write(buf, (size_t)n) != (size_t)n) {
      fail(String("Flash write failed: ") + Update.errorString());
      return;
    }
    _sha.update(buf, (size_t)n);
    _written += (size_t)n;
    if (_onProgress) _onProgress(_written, _job.size);
  }
}

void Pull::verify() {
  char digest[65];
  _sha.finishHex(digest);
  if (!_job.sha256.equalsIgnoreCase(digest)) {
    fail("The downloaded image does not match its SHA-256; nothing was installed");
    return;
  }

  // The size is exact, so there is nothing to pad; end() validates the image
  // and only then points the bootloader at it.
  if (!Update.end(false) || !Update.isFinished()) {
    _flashOpen = false;
    fail(String("The image was rejected: ") + Update.errorString());
    return;
  }
  _flashOpen = false;
  release();
  _phase = DONE;
}

void Pull::fail(const String &why) {
  _error = why;
  if (_flashOpen) {
    Update.abort();
    _flashOpen = false;
  }
  release();
  _phase = FAILED;
  log_w("[ClavdaOTA] Update %s failed: %s", _job.jobId.c_str(), why.c_str());
}

void Pull::release() {
  if (_http) {
    _http->end();
    delete _http;
    _http = nullptr;
  }
  if (_client) {
    delete _client;
    _client = nullptr;
  }
}

}  // namespace clavda
