/*
 * Cookie string utilities according to RFC 6265 4.1.1.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_COOKIE_STRING_HXX
#define BENG_PROXY_COOKIE_STRING_HXX

struct strref;

void
cookie_next_name_value(struct pool *pool, struct strref *input,
                       struct strref *name, struct strref *value,
                       bool rfc_ignorant);

#endif
