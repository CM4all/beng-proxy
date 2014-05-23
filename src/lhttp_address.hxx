/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LHTTP_ADDRESS_HXX
#define BENG_PROXY_LHTTP_ADDRESS_HXX

#include "child_options.hxx"
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

    /**
     * Environment variables.
     */
    struct param_array env;

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

    /**
     * Generates a string identifying the server process.  This can be
     * used as a key in a hash table.  The string will be allocated by
     * the specified pool.
     */
    gcc_pure
    const char *GetServerId(struct pool *pool) const;

    /**
     * Generates a string identifying the address.  This can be used as a
     * key in a hash table.  The string will be allocated by the specified
     * pool.
     */
    gcc_pure
    const char *GetId(struct pool *pool) const;

    bool Check(GError **error_r) const;

    /**
     * Duplicates this #lhttp_address object and inserts the specified
     * query string into the URI.
     */
    gcc_malloc
    struct lhttp_address *InsertQueryString(struct pool *pool,
                                            const char *query_string) const;

    /**
     * Duplicates this #lhttp_address object and inserts the specified
     * arguments into the URI.
     */
    gcc_malloc
    struct lhttp_address *InsertArgs(struct pool *pool,
                                     const char *new_args,
                                     size_t new_args_length,
                                     const char *path_info,
                                     size_t path_info_length) const;

    gcc_pure
    bool IsValidBase() const;

    struct lhttp_address *SaveBase(struct pool *pool,
                                   const char *suffix) const;

    struct lhttp_address *LoadBase(struct pool *pool,
                                   const char *suffix) const;

    /**
     * @return a new object on success, src if no change is needed, nullptr
     * on error
     */
    const struct lhttp_address *Apply(struct pool *pool, const char *relative,
                                      size_t relative_length) const;

    /**
     * Does this address need to be expanded with lhttp_address_expand()?
     */
    gcc_pure
    bool IsExpandable() const {
        return expand_uri != nullptr ||
            args.IsExpandable();
    }

    bool Expand(struct pool *pool, const GMatchInfo *match_info,
                GError **error_r);
};

void
lhttp_address_init(struct lhttp_address *address, const char *path);

struct lhttp_address *
lhttp_address_new(struct pool *pool, const char *path);

void
lhttp_address_copy(struct pool *pool, struct lhttp_address *dest,
                   const struct lhttp_address *src);

struct lhttp_address *
lhttp_address_dup(struct pool *pool, const struct lhttp_address *old);

struct lhttp_address *
lhttp_address_dup_with_uri(struct pool *pool, const struct lhttp_address *src,
                           const char *uri);

gcc_pure
const struct strref *
lhttp_address_relative(const struct lhttp_address *base,
                       const struct lhttp_address *address,
                       struct strref *buffer);

#endif
