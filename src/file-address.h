/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_FILE_ADDRESS_H
#define BENG_PROXY_FILE_ADDRESS_H

#include "jail.h"

#include <inline/compiler.h>

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
     * Should the delegate be jailed?
     */
    struct jail_params jail;
};

void
file_address_init(struct file_address *cgi, const char *path);

gcc_pure
const char *
file_address_uri(struct pool *pool, const struct file_address *cgi);

void
file_address_copy(struct pool *pool, struct file_address *dest,
                 const struct file_address *src);

bool
file_address_save_base(struct pool *pool, struct file_address *dest,
                      const struct file_address *src, const char *suffix);

void
file_address_load_base(struct pool *pool, struct file_address *dest,
                      const struct file_address *src, const char *suffix);

#endif
