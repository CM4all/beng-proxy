/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "cgi_address.hxx"
#include "pool.h"
#include "uri_base.hxx"
#include "uri-relative.h"
#include "uri-escape.h"
#include "uri-extract.h"
#include "regex.h"

#include <string.h>

void
cgi_address_init(struct cgi_address *cgi, const char *path,
                 bool have_address_list)
{
    assert(path != nullptr);

    memset(cgi, 0, sizeof(*cgi));
    cgi->path = path;

    param_array_init(&cgi->args);
    param_array_init(&cgi->env);
    child_options_init(&cgi->options);

    if (have_address_list)
        address_list_init(&cgi->address_list);
}

struct cgi_address *
cgi_address_new(struct pool *pool, const char *path, bool have_address_list)
{
    auto cgi = NewFromPool<struct cgi_address>(pool);
    cgi_address_init(cgi, path, have_address_list);
    return cgi;
}

const char *
cgi_address_uri(struct pool *pool, const struct cgi_address *cgi)
{
    if (cgi->uri != nullptr)
        return cgi->uri;

    const char *p = cgi->script_name;
    if (p == nullptr)
        p = "";

    if (cgi->path_info != nullptr)
        p = p_strcat(pool, p, cgi->path_info, nullptr);

    if (cgi->query_string != nullptr)
        p = p_strcat(pool, p, "?", cgi->query_string, nullptr);

    return p;
}

const char *
cgi_address_id(struct pool *pool, const struct cgi_address *address)
{
    char child_options_buffer[4096];
    *child_options_id(&address->options, child_options_buffer) = 0;

    const char *p = p_strcat(pool, address->path,
                             child_options_buffer,
                             nullptr);

    if (address->document_root != nullptr)
        p = p_strcat(pool, p, ";d=", address->document_root, nullptr);

    if (address->interpreter != nullptr)
        p = p_strcat(pool, p, ";i=", address->interpreter, nullptr);

    if (address->action != nullptr)
        p = p_strcat(pool, p, ";a=", address->action, nullptr);

    for (unsigned i = 0; i < address->args.n; ++i)
        p = p_strcat(pool, p, "!", address->args.values[i], nullptr);

    for (unsigned i = 0; i < address->env.n; ++i)
        p = p_strcat(pool, p, "$", address->env.values[i], nullptr);

    if (address->uri != nullptr)
        p = p_strcat(pool, p, ";u=", address->uri, nullptr);
    else if (address->script_name != nullptr)
        p = p_strcat(pool, p, ";s=", address->script_name, nullptr);

    if (address->path_info != nullptr)
        p = p_strcat(pool, p, ";p=", address->path_info, nullptr);

    if (address->query_string != nullptr)
        p = p_strcat(pool, p, "?", address->query_string, nullptr);

    return p;
}

void
cgi_address_copy(struct pool *pool, struct cgi_address *dest,
                 const struct cgi_address *src, bool have_address_list)
{
    assert(src->path != nullptr);

    dest->path = p_strdup(pool, src->path);

    param_array_copy(pool, &dest->args, &src->args);
    param_array_copy(pool, &dest->env, &src->env);

    child_options_copy(pool, &dest->options, &src->options);

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
    auto n = NewFromPool<struct cgi_address>(pool);
    cgi_address_copy(pool, n, old, have_address_list);
    return n;
}

char *
cgi_address_auto_base(struct pool *pool, const struct cgi_address *address,
                      const char *uri)
{
    /* auto-generate the BASE only if the path info begins with a
       slash and matches the URI */

    if (address->path_info == nullptr ||
        address->path_info[0] != '/' ||
        address->path_info[1] == 0)
        return nullptr;

    /* XXX implement (un-)escaping of the uri */

    size_t length = base_string(uri, address->path_info + 1);
    if (length == 0 || length == (size_t)-1)
        return nullptr;

    return p_strndup(pool, uri, length);
}

struct cgi_address *
cgi_address_save_base(struct pool *pool, const struct cgi_address *src,
                      const char *suffix, bool have_address_list)
{
    assert(pool != nullptr);
    assert(src != nullptr);
    assert(suffix != nullptr);

    if (src->path_info == nullptr)
        return nullptr;

    size_t uri_length = src->uri != nullptr
        ? base_string_unescape(pool, src->uri, suffix)
        : 0;
    if (uri_length == (size_t)-1)
        return nullptr;

    size_t length = base_string_unescape(pool, src->path_info, suffix);
    if (length == (size_t)-1)
        return nullptr;

    struct cgi_address *dest = cgi_address_dup(pool, src, have_address_list);
    if (dest->uri != nullptr)
        dest->uri = p_strndup(pool, dest->uri, uri_length);
    dest->path_info = p_strndup(pool, dest->path_info, length);
    return dest;
}

struct cgi_address *
cgi_address_load_base(struct pool *pool, const struct cgi_address *src,
                      const char *suffix, bool have_address_list)
{
    assert(pool != nullptr);
    assert(src != nullptr);
    assert(src->path_info != nullptr);
    assert(suffix != nullptr);

    char *unescaped = p_strdup(pool, suffix);
    unescaped[uri_unescape_inplace(unescaped, strlen(unescaped), '%')] = 0;

    struct cgi_address *dest = cgi_address_dup(pool, src, have_address_list);
    if (dest->uri != nullptr)
        dest->uri = p_strcat(pool, dest->uri, unescaped, nullptr);
    dest->path_info = p_strcat(pool, dest->path_info, unescaped, nullptr);
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
        return nullptr;

    const char *path_info = src->path_info != nullptr ? src->path_info : "";

    struct cgi_address *dest = cgi_address_dup(pool, src, have_address_list);
    dest->path_info = uri_absolute(pool, path_info,
                                   relative, relative_length);
    assert(dest->path_info != nullptr);
    return dest;
}

bool
cgi_address_expand(struct pool *pool, struct cgi_address *address,
                   const GMatchInfo *match_info, GError **error_r)
{
    assert(pool != nullptr);
    assert(address != nullptr);
    assert(match_info != nullptr);

    if (address->expand_path != nullptr) {
        address->path = expand_string_unescaped(pool, address->expand_path,
                                                match_info, error_r);
        if (address->path == nullptr)
            return false;
    }

    if (address->expand_path_info != nullptr) {
        address->path_info = expand_string_unescaped(pool, address->expand_path_info,
                                                     match_info, error_r);
        if (address->path_info == nullptr)
            return false;
    }

    return param_array_expand(pool, &address->args, match_info, error_r) &&
        param_array_expand(pool, &address->env, match_info, error_r);
}
