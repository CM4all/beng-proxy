/*
 * Hooks into external session managers.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SESSION_EXTERNAL_HXX
#define BENG_PROXY_SESSION_EXTERNAL_HXX

struct BpInstance;
struct Session;

/**
 * Check if the external session manager
 * (#TRANSLATE_EXTERNAL_SESSION_KEEPALIVE) needs to be refreshed, and
 * if yes, sends a HTTP GET request (as a background operation).
 */
void
RefreshExternalSession(BpInstance &instance, Session &session);

#endif
