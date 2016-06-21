/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CGI_ADDRESS_HXX
#define BENG_PROXY_CGI_ADDRESS_HXX

#include "spawn/ChildOptions.hxx"
#include "address_list.hxx"
#include "param_array.hxx"

#include <inline/compiler.h>

struct pool;
class MatchInfo;

/**
 * The address of a CGI/FastCGI/WAS request.
 */
struct CgiAddress {
    const char *path;

    /**
     * Command-line arguments.
     */
    struct param_array args;

    /**
     * Protocol-specific name/value pairs (per-request).
     */
    struct param_array params;

    ChildOptions options;

    const char *interpreter = nullptr;
    const char *action = nullptr;

    const char *uri = nullptr;
    const char *script_name = nullptr, *path_info = nullptr;
    const char *query_string = nullptr;
    const char *document_root = nullptr;

    /**
     * The value of #TRANSLATE_EXPAND_PATH.  Only used by the
     * translation cache.
     */
    const char *expand_path = nullptr;

    /**
     * The value of #TRANSLATE_EXPAND_URI.  Only used by the
     * translation cache.
     */
    const char *expand_uri = nullptr;

    /**
     * The value of #TRANSLATE_EXPAND_SCRIPT_NAME.  Only used by the
     * translation cache.
     */
    const char *expand_script_name = nullptr;

    /**
     * The value of #TRANSLATE_EXPAND_PATH_INFO.  Only used by
     * the translation cache.
     */
    const char *expand_path_info = nullptr;

    /**
     * The value of #TRANSLATE_EXPAND_DOCUMENT_ROOT.  Only used by the
     * translation cache.
     */
    const char *expand_document_root = nullptr;

    /**
     * An optional list of addresses to connect to.  If given
     * for a FastCGI resource, then beng-proxy connects to one
     * of the addresses instead of spawning a new child
     * process.
     */
    AddressList address_list;

    explicit CgiAddress(const char *_path);

    CgiAddress(struct pool &pool, const CgiAddress &src,
               bool have_address_list);

    gcc_pure
    const char *GetURI(struct pool *pool) const;

    /**
     * Generates a string identifying the address.  This can be used as a
     * key in a hash table.  The string will be allocated by the specified
     * pool.
     */
    gcc_pure
    const char *GetId(struct pool *pool) const;

    bool Check(GError **error_r) const {
        return options.Check(error_r);
    }

    gcc_pure
    bool HasQueryString() const {
        return query_string != nullptr && *query_string != 0;
    }

    CgiAddress *Clone(struct pool &p, bool have_address_list) const;

    gcc_pure
    bool IsValidBase() const;

    char *AutoBase(struct pool *pool, const char *request_uri) const;

    CgiAddress *SaveBase(struct pool *pool, const char *suffix,
                         bool have_address_list) const;

    CgiAddress *LoadBase(struct pool *pool, const char *suffix,
                         bool have_address_list) const;

    /**
     * @return a new object on success, src if no change is needed,
     * nullptr on error
     */
    const CgiAddress *Apply(struct pool *pool, const char *relative,
                                    size_t relative_length,
                                    bool have_address_list) const;

    /**
     * Does this address need to be expanded with Expand()?
     */
    gcc_pure
    bool IsExpandable() const {
        return options.IsExpandable() ||
            expand_path != nullptr ||
            expand_uri != nullptr ||
            expand_script_name != nullptr ||
            expand_path_info != nullptr ||
            expand_document_root != nullptr ||
            args.IsExpandable() ||
            params.IsExpandable();
    }

    bool Expand(struct pool *pool, const MatchInfo &match_info,
                Error &error_r);
};

CgiAddress *
cgi_address_new(struct pool &pool, const char *path);

#endif
