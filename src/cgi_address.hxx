/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CGI_ADDRESS_HXX
#define BENG_PROXY_CGI_ADDRESS_HXX

#include "child_options.hxx"
#include "address_list.hxx"
#include "param_array.hxx"
#include "glibfwd.hxx"

#include <inline/compiler.h>

struct pool;

/**
 * The address of a CGI/FastCGI/WAS request.
 */
struct cgi_address {
    const char *path;

    /**
     * Command-line arguments.
     */
    struct param_array args;

    /**
     * Environment variables (per-process).
     */
    struct param_array env;

    /**
     * Protocol-specific name/value pairs (per-request).
     */
    struct param_array params;

    struct child_options options;

    const char *interpreter;
    const char *action;

    const char *uri;
    const char *script_name, *path_info, *query_string;
    const char *document_root;

    /**
     * The value of #TRANSLATE_EXPAND_PATH.  Only used by the
     * translation cache.
     */
    const char *expand_path;

    /**
     * The value of #TRANSLATE_EXPAND_SCRIPT_NAME.  Only used by the
     * translation cache.
     */
    const char *expand_script_name;

    /**
     * The value of #TRANSLATE_EXPAND_PATH_INFO.  Only used by
     * the translation cache.
     */
    const char *expand_path_info;

    /**
     * An optional list of addresses to connect to.  If given
     * for a FastCGI resource, then beng-proxy connects to one
     * of the addresses instead of spawning a new child
     * process.
     */
    struct address_list address_list;

    gcc_pure
    const char *GetURI(struct pool *pool) const;

    /**
     * Generates a string identifying the address.  This can be used as a
     * key in a hash table.  The string will be allocated by the specified
     * pool.
     */
    gcc_pure
    const char *GetId(struct pool *pool) const;

    gcc_pure
    bool IsValidBase() const;

    char *AutoBase(struct pool *pool, const char *request_uri) const;

    struct cgi_address *SaveBase(struct pool *pool, const char *suffix,
                                 bool have_address_list) const;

    struct cgi_address *LoadBase(struct pool *pool, const char *suffix,
                                 bool have_address_list) const;

    /**
     * @return a new object on success, src if no change is needed,
     * nullptr on error
     */
    const struct cgi_address *Apply(struct pool *pool, const char *relative,
                                    size_t relative_length,
                                    bool have_address_list) const;

    /**
     * Does this address need to be expanded with Expand()?
     */
    gcc_pure
    bool IsExpandable() const {
        return expand_path != nullptr ||
            expand_script_name != nullptr ||
            expand_path_info != nullptr ||
            args.IsExpandable() ||
            env.IsExpandable() ||
            params.IsExpandable();
    }

    bool Expand(struct pool *pool, const GMatchInfo *match_info,
                GError **error_r);
};

void
cgi_address_init(struct cgi_address *cgi, const char *path,
                 bool have_address_list);

struct cgi_address *
cgi_address_new(struct pool &pool, const char *path,
                bool have_address_list);

void
cgi_address_copy(struct pool *pool, struct cgi_address *dest,
                 const struct cgi_address *src, bool have_address_list);

struct cgi_address *
cgi_address_dup(struct pool &pool, const struct cgi_address *old,
                bool have_address_list);

#endif
