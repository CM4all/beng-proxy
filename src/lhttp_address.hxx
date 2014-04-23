/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LHTTP_ADDRESS_HXX
#define BENG_PROXY_LHTTP_ADDRESS_HXX

#include "child_options.h"
#include "param_array.hxx"
#include "glibfwd.hxx"

#include <inline/compiler.h>

#include <assert.h>

struct pool;

/**
 * The address of a HTTP server that is launched and managed by
 * beng-proxy.
 */
struct lhttp_address {
    const char *path;

    struct param_array args;

    struct child_options options;

    /**
     * The host part of the URI (including the port, if any).
     */
    const char *host_and_port;

    const char *uri;

    /**
     * The value of #TRANSLATE_EXPAND_PATH.  Only used by the
     * translation cache.
     */
    const char *expand_uri;

    /**
     * The maximum number of concurrent connections to one instance.
     */
    unsigned concurrency;
};

void
lhttp_address_init(struct lhttp_address *address, const char *path);

struct lhttp_address *
lhttp_address_new(struct pool *pool, const char *path);

/**
 * Generates a string identifying the server process.  This can be
 * used as a key in a hash table.  The string will be allocated by the
 * specified pool.
 */
gcc_pure
const char *
lhttp_address_server_id(struct pool *pool, const struct lhttp_address *address);

/**
 * Generates a string identifying the address.  This can be used as a
 * key in a hash table.  The string will be allocated by the specified
 * pool.
 */
gcc_pure
const char *
lhttp_address_id(struct pool *pool, const struct lhttp_address *address);

void
lhttp_address_copy(struct pool *pool, struct lhttp_address *dest,
                   const struct lhttp_address *src);

struct lhttp_address *
lhttp_address_dup(struct pool *pool, const struct lhttp_address *old);

struct lhttp_address *
lhttp_address_dup_with_uri(struct pool *pool, const struct lhttp_address *src,
                           const char *uri);

/**
 * Duplicates this #lhttp_address object and inserts the specified
 * query string into the URI.
 */
gcc_malloc
struct lhttp_address *
lhttp_address_insert_query_string(struct pool *pool,
                                  const struct lhttp_address *src,
                                  const char *query_string);

/**
 * Duplicates this #lhttp_address object and inserts the specified
 * arguments into the URI.
 */
gcc_malloc
struct lhttp_address *
lhttp_address_insert_args(struct pool *pool,
                          const struct lhttp_address *src,
                          const char *args, size_t args_length,
                          const char *path, size_t path_length);

struct lhttp_address *
lhttp_address_save_base(struct pool *pool, const struct lhttp_address *src,
                        const char *suffix);

struct lhttp_address *
lhttp_address_load_base(struct pool *pool, const struct lhttp_address *src,
                        const char *suffix);

/**
 * @return a new object on success, src if no change is needed, nullptr
 * on error
 */
const struct lhttp_address *
lhttp_address_apply(struct pool *pool, const struct lhttp_address *src,
                    const char *relative, size_t relative_length);

gcc_pure
const struct strref *
lhttp_address_relative(const struct lhttp_address *base,
                       const struct lhttp_address *address,
                       struct strref *buffer);

/**
 * Does this address need to be expanded with lhttp_address_expand()?
 */
gcc_pure
static inline bool
lhttp_address_is_expandable(const struct lhttp_address *address)
{
    assert(address != nullptr);

    return address->expand_uri != nullptr ||
        address->args.IsExpandable();
}

bool
lhttp_address_expand(struct pool *pool, struct lhttp_address *address,
                     const GMatchInfo *match_info, GError **error_r);

#endif
