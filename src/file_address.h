/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_FILE_ADDRESS_H
#define BENG_PROXY_FILE_ADDRESS_H

#include "child_options.h"

#include <inline/compiler.h>

#include <glib.h>
#include <stdbool.h>

struct pool;

/**
 * The address of a local static file.
 */
struct file_address {
    const char *path;
    const char *deflated;
    const char *gzipped;
    const char *content_type;
    const char *delegate;
    const char *document_root;

    /**
     * The value of #TRANSLATE_EXPAND_PATH.  Only used by the
     * translation cache.
     */
    const char *expand_path;

    /**
     * Options for the delegate process.
     */
    struct child_options child_options;
};

void
file_address_init(struct file_address *cgi, const char *path);

struct file_address *
file_address_new(struct pool *pool, const char *path);

gcc_pure
const char *
file_address_uri(struct pool *pool, const struct file_address *cgi);

void
file_address_copy(struct pool *pool, struct file_address *dest,
                 const struct file_address *src);

struct file_address *
file_address_dup(struct pool *pool, const struct file_address *src);

struct file_address *
file_address_save_base(struct pool *pool, const struct file_address *src,
                       const char *suffix);

struct file_address *
file_address_load_base(struct pool *pool, const struct file_address *src,
                       const char *suffix);

/**
 * Does this address need to be expanded with file_address_expand()?
 */
gcc_pure
bool
file_address_is_expandable(const struct file_address *address);

bool
file_address_expand(struct pool *pool, struct file_address *address,
                    const GMatchInfo *match_info, GError **error_r);

#endif
