/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "cgi-address.h"
#include "pool.h"
#include "uri-base.h"
#include "uri-relative.h"
#include "uri-escape.h"
#include "uri-extract.h"
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

struct cgi_address *
cgi_address_new(struct pool *pool, const char *path, bool have_address_list)
{
    struct cgi_address *cgi = p_malloc(pool, sizeof(*cgi));
    cgi_address_init(cgi, path, have_address_list);
    return cgi;
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

    if (address->document_root != NULL)
        p = p_strcat(pool, p, ";d=", address->document_root, NULL);

    if (address->interpreter != NULL)
        p = p_strcat(pool, p, ";i=", address->interpreter, NULL);

    if (address->action != NULL)
        p = p_strcat(pool, p, ";a=", address->action, NULL);

    for (unsigned i = 0; i < address->num_args; ++i)
        p = p_strcat(pool, p, "!", address->args[i], NULL);

    if (address->uri != NULL)
        p = p_strcat(pool, p, ";u=", address->uri, NULL);
    else if (address->script_name != NULL)
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

struct cgi_address *
cgi_address_dup(struct pool *pool, const struct cgi_address *old,
                bool have_address_list)
{
    struct cgi_address *n = p_malloc(pool, sizeof(*n));
    cgi_address_copy(pool, n, old, have_address_list);
    return n;
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

struct cgi_address *
cgi_address_save_base(struct pool *pool, const struct cgi_address *src,
                      const char *suffix, bool have_address_list)
{
    assert(pool != NULL);
    assert(src != NULL);
    assert(suffix != NULL);

    if (src->path_info == NULL)
        return NULL;

    size_t uri_length = src->uri != NULL
        ? base_string_unescape(pool, src->uri, suffix)
        : 0;
    if (uri_length == (size_t)-1)
        return NULL;

    size_t length = base_string_unescape(pool, src->path_info, suffix);
    if (length == (size_t)-1)
        return NULL;

    struct cgi_address *dest = cgi_address_dup(pool, src, have_address_list);
    if (dest->uri != NULL)
        dest->uri = p_strndup(pool, dest->uri, uri_length);
    dest->path_info = p_strndup(pool, dest->path_info, length);
    return dest;
}

struct cgi_address *
cgi_address_load_base(struct pool *pool, const struct cgi_address *src,
                      const char *suffix, bool have_address_list)
{
    assert(pool != NULL);
    assert(src != NULL);
    assert(src->path_info != NULL);
    assert(suffix != NULL);

    char *unescaped = p_strdup(pool, suffix);
    unescaped[uri_unescape_inplace(unescaped, strlen(unescaped), '%')] = 0;

    struct cgi_address *dest = cgi_address_dup(pool, src, have_address_list);
    if (dest->uri != NULL)
        dest->uri = p_strcat(pool, dest->uri, unescaped, NULL);
    dest->path_info = p_strcat(pool, dest->path_info, unescaped, NULL);
    return dest;
}

const struct cgi_address *
cgi_address_apply(struct pool *pool, const struct cgi_address *src,
                  const char *relative, size_t relative_length,
                  bool have_address_list)
{
    if (relative_length == 0)
        return src;

    if (uri_has_protocol(relative, relative_length))
        return NULL;

    const char *path_info = src->path_info != NULL ? src->path_info : "";

    struct cgi_address *dest = cgi_address_dup(pool, src, have_address_list);
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
        address->path = expand_string_unescaped(pool, address->expand_path,
                                                match_info, error_r);
        if (address->path == NULL)
            return false;
    }

    if (address->expand_path_info != NULL) {
        address->path_info = expand_string_unescaped(pool, address->expand_path_info,
                                                     match_info, error_r);
        if (address->path_info == NULL)
            return false;
    }

    return true;
}
