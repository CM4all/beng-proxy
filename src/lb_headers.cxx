/*
 * Which headers should be forwarded to/from remote HTTP servers?
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lb_headers.hxx"
#include "strmap.hxx"
#include "pool.hxx"

#include <http/header.h>

#include <string.h>

static const char *const via_request_headers[] = {
    "via",
    "x-forwarded-for",
    "x-cm4all-beng-peer-subject",
    nullptr,
};

static void
forward_via(struct pool *pool, struct strmap *dest, const struct strmap *src,
            const char *local_host)
{
    const char *p;

    p = strmap_get_checked(src, "via");
    if (p == nullptr) {
        if (local_host != nullptr)
            dest->Add("via", p_strcat(pool, "1.1 ", local_host, nullptr));
    } else {
        if (local_host == nullptr)
            dest->Add("via", p);
        else
            dest->Add("via", p_strcat(pool, p, ", 1.1 ", local_host, nullptr));
    }
}

static void
forward_xff(struct pool *pool, struct strmap *dest, const struct strmap *src,
            const char *remote_host)
{
    const char *p;

    p = strmap_get_checked(src, "x-forwarded-for");
    if (p == nullptr) {
        if (remote_host != nullptr)
            dest->Add("x-forwarded-for", remote_host);
    } else {
        if (remote_host == nullptr)
            dest->Add("x-forwarded-for", p);
        else
            dest->Add("x-forwarded-for",
                      p_strcat(pool, p, ", ", remote_host, nullptr));
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
    for (unsigned i = 0; array[i] != nullptr; ++i)
        if (strcmp(array[i], value) == 0)
            return true;

    return false;
}

static void
forward_other_headers(struct strmap *dest, const struct strmap *src)
{
    for (const auto &i : *src)
        if (!string_in_array(via_request_headers, i.key) &&
            !http_header_is_hop_by_hop(i.key))
            dest->Add(i.key, i.value);
}

const struct strmap *
lb_forward_request_headers(struct pool *pool, const struct strmap *src,
                           const char *local_host, const char *remote_host,
                           const char *peer_subject,
                           const char *peer_issuer_subject,
                           bool mangle_via)
{
    if (peer_subject == nullptr && !mangle_via)
        return src;

    struct strmap *dest = strmap_new(pool);

    if (src != nullptr)
        forward_other_headers(dest, src);

    if (peer_subject != nullptr)
        dest->Add("x-cm4all-beng-peer-subject", peer_subject);

    if (peer_issuer_subject != nullptr)
        dest->Add("x-cm4all-beng-peer-issuer-subject", peer_issuer_subject);

    if (mangle_via)
        forward_identity(pool, dest, src, local_host, remote_host);

    return dest;
}
