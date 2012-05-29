/*
 * Which headers should be forwarded to/from remote HTTP servers?
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lb_headers.h"
#include "strmap.h"
#include "pool.h"

#include <http/header.h>

#include <string.h>

static const char *const via_request_headers[] = {
    "via",
    "x-forwarded-for",
    "x-cm4all-beng-peer-subject",
    NULL,
};

static void
forward_via(struct pool *pool, struct strmap *dest, const struct strmap *src,
            const char *local_host)
{
    const char *p;

    p = strmap_get_checked(src, "via");
    if (p == NULL) {
        if (local_host != NULL)
            strmap_add(dest, "via",
                       p_strcat(pool, "1.1 ", local_host, NULL));
    } else {
        if (local_host == NULL)
            strmap_add(dest, "via", p);
        else
            strmap_add(dest, "via",
                       p_strcat(pool, p, ", 1.1 ", local_host, NULL));
    }
}

static void
forward_xff(struct pool *pool, struct strmap *dest, const struct strmap *src,
            const char *remote_host)
{
    const char *p;

    p = strmap_get_checked(src, "x-forwarded-for");
    if (p == NULL) {
        if (remote_host != NULL)
            strmap_add(dest, "x-forwarded-for", remote_host);
    } else {
        if (remote_host == NULL)
            strmap_add(dest, "x-forwarded-for", p);
        else
            strmap_add(dest, "x-forwarded-for",
                       p_strcat(pool, p, ", ", remote_host, NULL));
    }
}

static void
forward_identity(struct pool *pool,
                 struct strmap *dest, const struct strmap *src,
                 const char *local_host, const char *remote_host)
{
    forward_via(pool, dest, src, local_host);
    forward_xff(pool, dest, src, remote_host);
}

static bool
string_in_array(const char *const array[], const char *value)
{
    for (unsigned i = 0; array[i] != NULL; ++i)
        if (strcmp(array[i], value) == 0)
            return true;

    return false;
}

static void
forward_other_headers(struct strmap *dest, struct strmap *src)
{
    const struct strmap_pair *pair;

    strmap_rewind(src);
    while ((pair = strmap_next(src)) != NULL)
        if (!string_in_array(via_request_headers, pair->key) &&
            !http_header_is_hop_by_hop(pair->key))
            strmap_add(dest, pair->key, pair->value);
}

struct strmap *
lb_forward_request_headers(struct pool *pool, struct strmap *src,
                           const char *local_host, const char *remote_host,
                           const char *peer_subject,
                           bool mangle_via)
{
    if (peer_subject == NULL && !mangle_via)
        return src;

    struct strmap *dest = strmap_new(pool, 32);

    if (src != NULL)
        forward_other_headers(dest, src);

    if (peer_subject != NULL)
        strmap_add(dest, "x-cm4all-beng-peer-subject", peer_subject);

    if (mangle_via)
        forward_identity(pool, dest, src, local_host, remote_host);

    return dest;
}
