/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "file_address.hxx"
#include "uri_base.hxx"
#include "uri-escape.h"
#include "regex.h"
#include "pool.h"

#include <assert.h>
#include <string.h>

void
file_address_init(struct file_address *address, const char *path)
{
    assert(address != nullptr);
    assert(path != nullptr);

    memset(address, 0, sizeof(*address));
    address->path = path;
    child_options_init(&address->child_options);
}

struct file_address *
file_address_new(struct pool *pool, const char *path)
{
    auto file = NewFromPool<struct file_address>(pool);
    file_address_init(file, path);
    return file;
}

void
file_address_copy(struct pool *pool, struct file_address *dest,
                  const struct file_address *src)
{
    assert(src->path != nullptr);
    dest->path = p_strdup(pool, src->path);
    dest->deflated = p_strdup_checked(pool, src->deflated);
    dest->gzipped = p_strdup_checked(pool, src->gzipped);
    dest->content_type =
        p_strdup_checked(pool, src->content_type);
    dest->delegate = p_strdup_checked(pool, src->delegate);
    dest->document_root =
        p_strdup_checked(pool, src->document_root);

    dest->expand_path = p_strdup_checked(pool, src->expand_path);

    child_options_copy(pool, &dest->child_options, &src->child_options);
}

struct file_address *
file_address_dup(struct pool *pool, const struct file_address *src)
{
    auto dest = NewFromPool<struct file_address>(pool);
    file_address_copy(pool, dest, src);
    return dest;
}

struct file_address *
file_address_save_base(struct pool *pool, const struct file_address *src,
                       const char *suffix)
{
    assert(pool != nullptr);
    assert(src != nullptr);
    assert(suffix != nullptr);

    size_t length = base_string_unescape(pool, src->path, suffix);
    if (length == (size_t)-1)
        return nullptr;

    struct file_address *dest = file_address_dup(pool, src);
    dest->path = p_strndup(pool, dest->path, length);

    /* BASE+DEFLATED is not supported */
    dest->deflated = nullptr;
    dest->gzipped = nullptr;

    return dest;
}

struct file_address *
file_address_load_base(struct pool *pool, const struct file_address *src,
                       const char *suffix)
{
    assert(pool != nullptr);
    assert(src != nullptr);
    assert(src->path != nullptr);
    assert(*src->path != 0);
    assert(src->path[strlen(src->path) - 1] == '/');
    assert(suffix != nullptr);

    char *unescaped = p_strdup(pool, suffix);
    unescaped[uri_unescape_inplace(unescaped, strlen(unescaped), '%')] = 0;

    struct file_address *dest = file_address_dup(pool, src);
    dest->path = p_strcat(pool, dest->path, unescaped, nullptr);
    return dest;
}

bool
file_address_is_expandable(const struct file_address *address)
{
    assert(address != nullptr);

    return address->expand_path != nullptr;
}

bool
file_address_expand(struct pool *pool, struct file_address *address,
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

    return true;
}
