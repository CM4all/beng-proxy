/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CGI_ADDRESS_HXX
#define BENG_PROXY_CGI_ADDRESS_HXX

#include "spawn/ChildOptions.hxx"
#include "address_list.hxx"
#include "ExpandableStringList.hxx"

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
    ExpandableStringList args;

    /**
     * Protocol-specific name/value pairs (per-request).
     */
    ExpandableStringList params;

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

    constexpr CgiAddress(ShallowCopy shallow_copy, const CgiAddress &src)
        :path(src.path),
         args(shallow_copy, src.args), params(shallow_copy, src.params),
         options(shallow_copy, src.options),
         interpreter(src.interpreter), action(src.action),
         uri(src.uri), script_name(src.script_name), path_info(src.path_info),
         query_string(src.query_string), document_root(src.document_root),
         expand_path(src.expand_path),
         expand_uri(src.expand_uri),
         expand_script_name(src.expand_script_name),
         expand_path_info(src.expand_path_info),
         expand_document_root(src.expand_document_root),
         address_list(shallow_copy, src.address_list)
    {
    }

    CgiAddress(struct pool &pool, const CgiAddress &src);

    gcc_pure
    const char *GetURI(struct pool *pool) const;

    /**
     * Generates a string identifying the address.  This can be used as a
     * key in a hash table.  The string will be allocated by the specified
     * pool.
     */
    gcc_pure
    const char *GetId(struct pool *pool) const;

    void Check() const {
        options.Check();
    }

    gcc_pure
    bool HasQueryString() const {
        return query_string != nullptr && *query_string != 0;
    }

    void InsertQueryString(struct pool &pool, const char *new_query_string);

    void InsertArgs(struct pool &pool, StringView new_args,
                    StringView new_path_info);

    CgiAddress *Clone(struct pool &p) const;

    gcc_pure
    bool IsValidBase() const;

    char *AutoBase(struct pool *pool, const char *request_uri) const;

    CgiAddress *SaveBase(struct pool *pool, const char *suffix) const;

    CgiAddress *LoadBase(struct pool *pool, const char *suffix) const;

    /**
     * @return a new object on success, src if no change is needed,
     * nullptr on error
     */
    const CgiAddress *Apply(struct pool *pool, StringView relative) const;

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
