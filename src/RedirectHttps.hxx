/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef REDIRECT_HTTPS_HXX
#define REDIRECT_HTTPS_HXX

#include <stdint.h>

struct pool;

/**
 * Generate a "https://" redirect URI for the current request.
 *
 * @param host the Host request header
 * @param port the new port; 0 means default
 * @param uri the request URI
 */
const char *
MakeHttpsRedirect(struct pool &p, const char *host, uint16_t port,
                  const char *uri);

#endif
