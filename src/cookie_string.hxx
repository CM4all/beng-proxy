/*
 * Cookie string utilities according to RFC 6265 4.1.1.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_COOKIE_STRING_HXX
#define BENG_PROXY_COOKIE_STRING_HXX

struct pool;
struct StringView;

void
cookie_next_name_value(struct pool &pool, StringView &input,
                       StringView &name, StringView &value,
                       bool rfc_ignorant);

#endif
