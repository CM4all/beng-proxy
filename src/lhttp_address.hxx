/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LHTTP_ADDRESS_HXX
#define BENG_PROXY_LHTTP_ADDRESS_HXX

#include "spawn/ChildOptions.hxx"
#include "param_array.hxx"
#include "glibfwd.hxx"

#include <inline/compiler.h>

#include <assert.h>

struct pool;
struct StringView;
class Error;

/**
 * The address of a HTTP server that is launched and managed by
 * beng-proxy.
 */
struct LhttpAddress {
    const char *path;

    struct param_array args;

    /**
     * Environment variables.
     */
    struct param_array env;

    ChildOptions options;

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
     * Pass a blocking listener socket to the child process?  The
     * default is true; sets SOCK_NONBLOCK if false.
     */
    bool blocking;

    explicit LhttpAddress(const char *path);
    LhttpAddress(struct pool &pool, const LhttpAddress &src);

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

    gcc_pure
    bool HasQueryString() const;

    LhttpAddress *Dup(struct pool &pool) const;

    LhttpAddress *DupWithUri(struct pool &pool, const char *uri) const;

    /**
     * Duplicates this #lhttp_address object and inserts the specified
     * query string into the URI.
     */
    gcc_malloc
    LhttpAddress *InsertQueryString(struct pool &pool,
                                    const char *query_string) const;

    /**
     * Duplicates this #lhttp_address object and inserts the specified
     * arguments into the URI.
     */
    gcc_malloc
    LhttpAddress *InsertArgs(struct pool &pool,
                             const char *new_args,
                             size_t new_args_length,
                             const char *path_info,
                             size_t path_info_length) const;

    gcc_pure
    bool IsValidBase() const;

    LhttpAddress *SaveBase(struct pool *pool,
                           const char *suffix) const;

    LhttpAddress *LoadBase(struct pool *pool,
                           const char *suffix) const;

    /**
     * @return a new object on success, src if no change is needed, nullptr
     * on error
     */
    const LhttpAddress *Apply(struct pool *pool, const char *relative,
                              size_t relative_length) const;

    gcc_pure
    StringView RelativeTo(const LhttpAddress &base) const;

    /**
     * Does this address need to be expanded with lhttp_address_expand()?
     */
    gcc_pure
    bool IsExpandable() const {
        return options.IsExpandable() ||
            expand_uri != nullptr ||
            args.IsExpandable();
    }

    bool Expand(struct pool *pool, const MatchInfo &match_info,
                Error &error_r);
};

#endif
