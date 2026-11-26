#include "ClavdaPull.h"

#include <stdlib.h>
#include "ClavdaPlatform.h"

namespace clavda {

// No bytes for this long counts as a stalled download.
static const uint32_t STALL_MS = 20000;
// Connecting and the TLS handshake with the download host.
static const int CONNECT_TIMEOUT_MS = 15000;
// Longest a single loop() spends writing flash before handing control back.
static const uint32_t SLICE_MS = 50;
// Read and written a piece at a time: a quarter of a TLS record.
static const size_t CHUNK = 4096;
static const int MAX_REDIRECTS = 3;

Pull::~Pull() {
  release();
}

bool Pull::start(const Job &job, const char *caRoots) {
  if (busy()) return false;
  release();
  _job = job;
  _caRoots = caRoots;
  _written = 0;
  _error.clear();
  _flashOpen = false;
  _lastData = nowMs();
  _phase = CONNECTING;
  return true;
}

void Pull::reset() {
  if (busy()) return;
  release();
  _phase = IDLE;
  _written = 0;
  _error.clear();
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

/** Where a redirect points, made absolute against the URL it came from. */
static std::string resolve(const Url &from, const std::string &location) {
  if (location.find("://") != std::string::npos) return location;
  const std::string origin =
    std::string(from.tls ? "https://" : "http://") + from.hostHeader();
  if (!location.empty() && location[0] == '/') return origin + location;
  const size_t slash = from.path.rfind('/', from.path.find('?'));
  return origin + from.path.substr(0, slash + 1) + location;
}

void Pull::open() {
  std::string url = _job.url;
  const bool wantTls = startsWith(url, "https://");
  std::string head;
  std::string body;
  for (int hop = 0;; hop++) {
    Url target;
    if (!parseUrl(url, target)) return fail("The download address is not a valid URL");
    // A redirect may not drop the TLS the job asked for.
    if (wantTls && !target.tls) return fail("The download host redirected to plain http");

    std::string error;
    if (!_conn.open(target, _caRoots, CONNECT_TIMEOUT_MS, error)) {
      return fail("Could not reach the download host: " + error);
    }
    const std::string request = format("GET %s HTTP/1.1\r\n"
                                       "Host: %s\r\n"
                                       "User-Agent: ClavdaOTA/" CLAVDA_LIB_VERSION "\r\n"
                                       "Accept-Encoding: identity\r\n"
                                       "Connection: close\r\n"
                                       "\r\n",
                                       target.path.c_str(), target.hostHeader().c_str());
    if (!_conn.write(request.data(), request.size(), CONNECT_TIMEOUT_MS)) {
      return fail("Could not send the download request");
    }
    if (!_conn.readHead((int)STALL_MS, head, body, error)) {
      return fail("The download host did not answer: " + error);
    }

    const int code = httpStatus(head);
    const bool redirect = code == 301 || code == 302 || code == 303 || code == 307 || code == 308;
    const std::string location = httpHeader(head, "location");
    if (redirect && !location.empty() && hop < MAX_REDIRECTS) {
      _conn.close();
      url = resolve(target, location);
      continue;
    }
    // 403 is what S3 answers once the presigned link has expired.
    if (code != 200) return fail(format("Download failed (HTTP %d)", code));
    break;
  }

  if (equalsIgnoreCase(httpHeader(head, "transfer-encoding").c_str(), "chunked")) {
    return fail("The download host sent no length for the image");
  }
  const std::string length = httpHeader(head, "content-length");
  const unsigned long long size = length.empty() ? 0 : strtoull(length.c_str(), nullptr, 10);
  if (length.empty() || size != _job.size) {
    return fail(format("Download is %s bytes, the job expects %u",
                       length.empty() ? "an unknown number of" : length.c_str(),
                       (unsigned)_job.size));
  }

  _slot = esp_ota_get_next_update_partition(nullptr);
  if (!_slot) return fail("This partition table has no OTA slot");
#ifdef OTA_WITH_SEQUENTIAL_WRITES
  // Each sector is erased as the image reaches it, rather than the whole slot up front.
  const size_t eraseSize = OTA_WITH_SEQUENTIAL_WRITES;
#else
  const size_t eraseSize = _job.size;
#endif
  const esp_err_t err = esp_ota_begin(_slot, eraseSize, &_ota);
  if (err != ESP_OK) return fail(format("Cannot start the update: %s", esp_err_to_name(err)));
  _flashOpen = true;

  _buf.reset(new (std::nothrow) uint8_t[CHUNK]);
  if (!_buf) return fail("Out of memory for the download buffer");
  _sha.begin();
  _lastData = nowMs();
  _phase = DOWNLOADING;
  // The start of the body may have come in with the headers.
  if (!body.empty()) {
    consume((const uint8_t *)body.data(),
            body.size() < _job.size ? body.size() : _job.size);
  }
}

bool Pull::consume(const uint8_t *data, size_t len) {
  const esp_err_t err = esp_ota_write(_ota, data, len);
  if (err != ESP_OK) {
    fail(format("Flash write failed: %s", esp_err_to_name(err)));
    return false;
  }
  _sha.update(data, len);
  _written += len;
  _lastData = nowMs();
  if (_onProgress) _onProgress(_written, _job.size);
  return true;
}

void Pull::stream() {
  const uint32_t sliceEnd = nowMs() + SLICE_MS;
  while ((int32_t)(nowMs() - sliceEnd) < 0) {
    if (_written >= _job.size) {
      _phase = VERIFYING;
      return;
    }
    size_t want = _job.size - _written;
    if (want > CHUNK) want = CHUNK;
    const int n = _conn.read(_buf.get(), want);
    if (n < 0) {
      return fail(format("The download ended early at %u of %u bytes", (unsigned)_written,
                         (unsigned)_job.size));
    }
    if (n == 0) {
      if (nowMs() - _lastData > STALL_MS) fail("The download stalled");
      return;
    }
    if (!consume(_buf.get(), (size_t)n)) return;
  }
}

void Pull::verify() {
  char digest[65];
  _sha.finishHex(digest);
  if (!equalsIgnoreCase(_job.sha256.c_str(), digest)) {
    return fail("The downloaded image does not match its SHA-256; nothing was installed");
  }

  // Validates the image; the handle is gone afterwards, whatever the result.
  _flashOpen = false;
  esp_err_t err = esp_ota_end(_ota);
  if (err != ESP_OK) return fail(format("The image was rejected: %s", esp_err_to_name(err)));
  // Only now does the bootloader point at the new image.
  err = esp_ota_set_boot_partition(_slot);
  if (err != ESP_OK) return fail(format("Cannot boot the new image: %s", esp_err_to_name(err)));
  release();
  _phase = DONE;
}

void Pull::fail(const std::string &why) {
  _error = why;
  if (_flashOpen) {
    esp_ota_abort(_ota);
    _flashOpen = false;
  }
  release();
  _phase = FAILED;
  CLAVDA_LOGW("Update %s failed: %s", _job.jobId.c_str(), why.c_str());
}

void Pull::release() {
  _conn.close();
  _buf.reset();
}

}  // namespace clavda
