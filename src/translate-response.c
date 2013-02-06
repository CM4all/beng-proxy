/*
 * The translation response struct.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "translate-response.h"
#include "pool.h"
#include "strref-pool.h"
#include "strmap.h"
#include "widget-view.h"

void
translate_response_copy(struct pool *pool, struct translate_response *dest,
                        const struct translate_response *src)
{
    /* we don't copy the "max_age" attribute, because it's only used
       by the tcache itself */

    dest->status = src->status;

    dest->request_header_forward = src->request_header_forward;
    dest->response_header_forward = src->response_header_forward;

    dest->base = p_strdup_checked(pool, src->base);
    dest->regex = p_strdup_checked(pool, src->regex);
    dest->inverse_regex = p_strdup_checked(pool, src->inverse_regex);
    dest->site = p_strdup_checked(pool, src->site);
    dest->document_root = p_strdup_checked(pool, src->document_root);
    dest->redirect = p_strdup_checked(pool, src->redirect);
    dest->bounce = p_strdup_checked(pool, src->bounce);
    dest->scheme = p_strdup_checked(pool, src->scheme);
    dest->host = p_strdup_checked(pool, src->host);
    dest->uri = p_strdup_checked(pool, src->uri);
    dest->local_uri = p_strdup_checked(pool, src->local_uri);
    dest->untrusted = p_strdup_checked(pool, src->untrusted);
    dest->untrusted_prefix = p_strdup_checked(pool, src->untrusted_prefix);
    dest->untrusted_site_suffix =
        p_strdup_checked(pool, src->untrusted_site_suffix);
    dest->direct_addressing = src->direct_addressing;
    dest->stateful = src->stateful;
    dest->discard_session = src->discard_session;
    dest->secure_cookie = src->secure_cookie;
    dest->filter_4xx = src->filter_4xx;
    dest->error_document = src->error_document;
    dest->previous = src->previous;
    dest->transparent = src->transparent;
    dest->auto_base = src->auto_base;
    dest->widget_info = src->widget_info;
    dest->widget_group = p_strdup_checked(pool, src->widget_group);

    strset_init(&dest->container_groups);
    strset_copy(pool, &dest->container_groups, &src->container_groups);

    dest->anchor_absolute = src->anchor_absolute;
    dest->dump_headers = src->dump_headers;
    dest->session = NULL;

    if (strref_is_null(&src->check))
        strref_null(&dest->check);
    else
        strref_set_dup(pool, &dest->check, &src->check);

    /* The "user" attribute must not be present in cached responses,
       because they belong to only that one session.  For the same
       reason, we won't copy the user_max_age attribute. */
    dest->user = NULL;

    dest->language = NULL;
    dest->realm = p_strdup_checked(pool, src->realm);
    dest->www_authenticate = p_strdup_checked(pool, src->www_authenticate);
    dest->authentication_info = p_strdup_checked(pool,
                                                 src->authentication_info);
    dest->cookie_domain = p_strdup_checked(pool, src->cookie_domain);
    dest->cookie_host = p_strdup_checked(pool, src->cookie_host);

    dest->headers = src->headers != NULL
        ? strmap_dup(pool, src->headers, 17)
        : NULL;

    dest->views = src->views != NULL
        ? widget_view_dup_chain(pool, src->views)
        : NULL;

    dest->num_vary = src->num_vary;
    if (dest->num_vary > 0)
        dest->vary = (const uint16_t *)
            p_memdup(pool, src->vary,
                     dest->num_vary * sizeof(dest->vary[0]));

    dest->num_invalidate = src->num_invalidate;
    if (dest->num_invalidate > 0)
        dest->invalidate = (const uint16_t *)
            p_memdup(pool, src->invalidate,
                     dest->num_invalidate * sizeof(dest->invalidate[0]));

    dest->validate_mtime.mtime = src->validate_mtime.mtime;
    dest->validate_mtime.path =
        p_strdup_checked(pool, src->validate_mtime.path);
}

bool
translate_response_is_expandable(const struct translate_response *response)
{
    return response->regex != NULL &&
        (resource_address_is_expandable(&response->address) ||
         widget_view_any_is_expandable(response->views));
}

bool
translate_response_expand(struct pool *pool,
                          struct translate_response *response,
                          const GMatchInfo *match_info, GError **error_r)
{
    assert(pool != NULL);
    assert(response != NULL);
    assert(response->regex != NULL);
    assert(match_info != NULL);

    return resource_address_expand(pool, &response->address,
                                   match_info, error_r) &&
        widget_view_expand_all(pool, response->views, match_info, error_r);
}
