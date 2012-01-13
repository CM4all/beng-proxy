/*
 * Store a URI along with a list of socket addresses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "uri-address.h"
#include "uri-edit.h"
#include "uri-relative.h"
#include "pool.h"
#include "strref.h"

#include <socket/address.h>

#include <string.h>

struct uri_with_address *
uri_address_new(struct pool *pool, const char *uri)
{
    struct uri_with_address *uwa = p_malloc(pool, sizeof(*uwa));
    uwa->uri = p_strdup(pool, uri);
    address_list_init(&uwa->addresses);

    return uwa;
}

struct uri_with_address *
uri_address_dup(struct pool *pool, const struct uri_with_address *uwa)
{
    struct uri_with_address *p = p_malloc(pool, sizeof(*uwa));

    p->uri = p_strdup(pool, uwa->uri);

    address_list_copy(pool, &p->addresses, &uwa->addresses);

    return p;
}

struct uri_with_address *
uri_address_insert_query_string(struct pool *pool,
                                const struct uri_with_address *uwa,
                                const char *query_string)
{
    struct uri_with_address *p = p_malloc(pool, sizeof(*uwa));

    p->uri = uri_insert_query_string(pool, uwa->uri, query_string);

    address_list_copy(pool, &p->addresses, &uwa->addresses);

    return p;
}

struct uri_with_address *
uri_address_insert_args(struct pool *pool,
                        const struct uri_with_address *uwa,
                        const char *args, size_t length)
{
    struct uri_with_address *p = p_malloc(pool, sizeof(*uwa));

    p->uri = uri_insert_args(pool, uwa->uri, args, length);

    address_list_copy(pool, &p->addresses, &uwa->addresses);

    return p;
}

const struct strref *
uri_address_relative(const struct uri_with_address *base,
                     const struct uri_with_address *uwa,
                     struct strref *buffer)
{
    struct strref base_uri;
    strref_set_c(&base_uri, base->uri);
    strref_set_c(buffer, uwa->uri);
    return uri_relative(&base_uri, buffer);
}
