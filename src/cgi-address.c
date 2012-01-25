/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "cgi-address.h"
#include "pool.h"
#include "uri-base.h"
#include "uri-relative.h"
#include "uri-escape.h"
#include "regex.h"

#include <string.h>

void
cgi_address_init(struct cgi_address *cgi, const char *path,
                 bool have_address_list)
{
    assert(path != NULL);

    memset(cgi, 0, sizeof(*cgi));
    cgi->path = path;

    if (have_address_list)
        address_list_init(&cgi->address_list);
}

const char *
cgi_address_uri(struct pool *pool, const struct cgi_address *cgi)
{
    if (cgi->uri != NULL)
        return cgi->uri;

    const char *p = cgi->script_name;
    if (p == NULL)
        p = "";

    if (cgi->path_info != NULL)
        p = p_strcat(pool, p, cgi->path_info, NULL);

    if (cgi->query_string != NULL)
        p = p_strcat(pool, p, "?", cgi->query_string, NULL);

    return p;
}

const char *
cgi_address_id(struct pool *pool, const struct cgi_address *address)
{
    const char *p = p_strdup(pool, address->path);

    if (address->jail.enabled)
        p = p_strcat(pool, p, ";j", NULL);

    if (address->interpreter != NULL)
        p = p_strcat(pool, p, ";i=", address->interpreter, NULL);

    if (address->action != NULL)
        p = p_strcat(pool, p, ";a=", address->action, NULL);

    for (unsigned i = 0; i < address->num_args; ++i)
        p = p_strcat(pool, p, "!", address->args[i], NULL);

    if (address->script_name != NULL)
        p = p_strcat(pool, p, ";s=", address->script_name, NULL);

    if (address->path_info != NULL)
        p = p_strcat(pool, p, ";p=", address->path_info, NULL);

    if (address->query_string != NULL)
        p = p_strcat(pool, p, "?", address->query_string, NULL);

    return p;
}

void
cgi_address_copy(struct pool *pool, struct cgi_address *dest,
                 const struct cgi_address *src, bool have_address_list)
{
    assert(src->path != NULL);

    dest->path = p_strdup(pool, src->path);

    for (unsigned i = 0; i < src->num_args; ++i)
        dest->args[i] = p_strdup(pool, src->args[i]);
    dest->num_args = src->num_args;

    jail_params_copy(pool, &dest->jail, &src->jail);

    dest->interpreter =
        p_strdup_checked(pool, src->interpreter);
    dest->action = p_strdup_checked(pool, src->action);
    dest->uri =
        p_strdup_checked(pool, src->uri);
    dest->script_name =
        p_strdup_checked(pool, src->script_name);
    dest->path_info = p_strdup_checked(pool, src->path_info);
    dest->expand_path = p_strdup_checked(pool, src->expand_path);
    dest->expand_path_info =
        p_strdup_checked(pool, src->expand_path_info);
    dest->query_string =
        p_strdup_checked(pool, src->query_string);
    dest->document_root =
        p_strdup_checked(pool, src->document_root);

    if (have_address_list)
        address_list_copy(pool, &dest->address_list, &src->address_list);
}

char *
cgi_address_auto_base(struct pool *pool, const struct cgi_address *address,
                      const char *uri)
{
    /* auto-generate the BASE only if the path info begins with a
       slash and matches the URI */

    if (address->path_info == NULL ||
        address->path_info[0] != '/' ||
        address->path_info[1] == 0)
        return NULL;

    /* XXX implement (un-)escaping of the uri */

    size_t length = base_string(uri, address->path_info + 1);
    if (length == 0 || length == (size_t)-1)
        return NULL;

    return p_strndup(pool, uri, length);
}

bool
cgi_address_save_base(struct pool *pool, struct cgi_address *dest,
                      const struct cgi_address *src, const char *suffix,
                      bool have_address_list)
{
    assert(pool != NULL);
    assert(dest != NULL);
    assert(src != NULL);
    assert(suffix != NULL);

    if (src->path_info == NULL)
        return false;

    size_t length = base_string_unescape(pool, src->path_info, suffix);
    if (length == (size_t)-1)
        return false;

    cgi_address_copy(pool, dest, src, have_address_list);
    dest->path_info = p_strndup(pool, dest->path_info, length);
    return true;
}

void
cgi_address_load_base(struct pool *pool, struct cgi_address *dest,
                      const struct cgi_address *src, const char *suffix,
                      bool have_address_list)
{
    assert(pool != NULL);
    assert(dest != NULL);
    assert(src != NULL);
    assert(src->path_info != NULL);
    assert(suffix != NULL);

    char *unescaped = p_strdup(pool, suffix);
    unescaped[uri_unescape_inplace(unescaped, strlen(unescaped), '%')] = 0;

    cgi_address_copy(pool, dest, src, have_address_list);
    dest->path_info = p_strcat(pool, dest->path_info, unescaped, NULL);

}

const struct cgi_address *
cgi_address_apply(struct pool *pool, struct cgi_address *dest,
                  const struct cgi_address *src,
                  const char *relative, size_t relative_length,
                  bool have_address_list)
{
    if (relative_length == 0)
        return src;

    const char *path_info = src->path_info != NULL ? src->path_info : "";

    cgi_address_copy(pool, dest, src, have_address_list);
    dest->path_info = uri_absolute(pool, path_info,
                                   relative, relative_length);
    assert(dest->path_info != NULL);
    return dest;
}

bool
cgi_address_expand(struct pool *pool, struct cgi_address *address,
                   const GMatchInfo *match_info, GError **error_r)
{
    assert(pool != NULL);
    assert(address != NULL);
    assert(match_info != NULL);

    if (address->expand_path != NULL) {
        address->path = expand_string(pool, address->expand_path,
                                      match_info, error_r);
        if (address->path == NULL)
            return false;
    }

    if (address->expand_path_info != NULL) {
        address->path_info = expand_string(pool, address->expand_path_info,
                                           match_info, error_r);
        if (address->path_info == NULL)
            return false;
    }

    return true;
}
