/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "file_address.h"
#include "uri-base.h"
#include "uri-escape.h"
#include "regex.h"
#include "pool.h"

#include <assert.h>
#include <string.h>

void
file_address_init(struct file_address *address, const char *path)
{
    assert(address != NULL);
    assert(path != NULL);

    memset(address, 0, sizeof(*address));
    address->path = path;
}

struct file_address *
file_address_new(struct pool *pool, const char *path)
{
    struct file_address *file = p_malloc(pool, sizeof(*file));
    file_address_init(file, path);
    return file;
}

void
file_address_copy(struct pool *pool, struct file_address *dest,
                  const struct file_address *src)
{
    assert(src->path != NULL);
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
    struct file_address *dest = p_malloc(pool, sizeof(*dest));
    file_address_copy(pool, dest, src);
    return dest;
}

struct file_address *
file_address_save_base(struct pool *pool, const struct file_address *src,
                       const char *suffix)
{
    assert(pool != NULL);
    assert(src != NULL);
    assert(suffix != NULL);

    size_t length = base_string_unescape(pool, src->path, suffix);
    if (length == (size_t)-1)
        return NULL;

    struct file_address *dest = file_address_dup(pool, src);
    dest->path = p_strndup(pool, dest->path, length);

    /* BASE+DEFLATED is not supported */
    dest->deflated = NULL;
    dest->gzipped = NULL;

    return dest;
}

struct file_address *
file_address_load_base(struct pool *pool, const struct file_address *src,
                       const char *suffix)
{
    assert(pool != NULL);
    assert(src != NULL);
    assert(src->path != NULL);
    assert(*src->path != 0);
    assert(src->path[strlen(src->path) - 1] == '/');
    assert(suffix != NULL);

    char *unescaped = p_strdup(pool, suffix);
    unescaped[uri_unescape_inplace(unescaped, strlen(unescaped), '%')] = 0;

    struct file_address *dest = file_address_dup(pool, src);
    dest->path = p_strcat(pool, dest->path, unescaped, NULL);
    return dest;
}

bool
file_address_is_expandable(const struct file_address *address)
{
    assert(address != NULL);

    return address->expand_path != NULL;
}

bool
file_address_expand(struct pool *pool, struct file_address *address,
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

    return true;
}
