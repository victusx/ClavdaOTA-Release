#ifndef ClavdaSession_h
#define ClavdaSession_h

#include <Arduino.h>
#include "ClavdaIdentity.h"

namespace clavda {

/** What a console-sealed session grants once it verifies. */
struct SessionGrant {
  /** The session's own AES-256 key; wipe it once copied. */
  uint8_t key[32];
  /** The caller may send input (edit permission on devices). */
  bool input;
};

/**
 * Open a session the console sealed with this device's job key - a Serial
 * Monitor or a dashboard - and check it: `kind` and `sessionId` must match what
 * was sealed, it must be for this device, and it must not have expired. A
 * refusal fills `code` (invalid / expired) and `why`, which names the session
 * as `what` ("monitor session", "dashboard session").
 */
bool openSession(const char *kind, const char *what, const char *sessionId, const char *enc,
                 Identity &identity, SessionGrant &grant, const char *&code, String &why);

}  // namespace clavda

#endif
