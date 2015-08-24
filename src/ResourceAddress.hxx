/*
 * Address of a resource, which might be a local file, a CGI script or
 * a HTTP server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_RESOURCE_ADDRESS_HXX
#define BENG_PROXY_RESOURCE_ADDRESS_HXX

#include "glibfwd.hxx"

#include <inline/compiler.h>

#include <cstddef>

#include <assert.h>

struct pool;
struct strref;
struct LhttpAddress;
class MatchInfo;
class Error;

enum resource_address_type {
    RESOURCE_ADDRESS_NONE = 0,
    RESOURCE_ADDRESS_LOCAL,
    RESOURCE_ADDRESS_HTTP,
    RESOURCE_ADDRESS_LHTTP,
    RESOURCE_ADDRESS_PIPE,
    RESOURCE_ADDRESS_CGI,
    RESOURCE_ADDRESS_FASTCGI,
    RESOURCE_ADDRESS_WAS,
    RESOURCE_ADDRESS_AJP,
    RESOURCE_ADDRESS_NFS,
};

struct ResourceAddress {
    enum resource_address_type type;

    union U {
        const struct file_address *file;

        const struct http_address *http;

        const LhttpAddress *lhttp;

        const struct cgi_address *cgi;

        const struct nfs_address *nfs;

        U() = default;
        constexpr U(std::nullptr_t n):file(n) {}
        constexpr U(const struct file_address &_file):file(&_file) {}
        constexpr U(const struct http_address &_http):http(&_http) {}
        constexpr U(const LhttpAddress &_lhttp):lhttp(&_lhttp) {}
        constexpr U(const struct cgi_address &_cgi):cgi(&_cgi) {}
        constexpr U(const struct nfs_address &_nfs):nfs(&_nfs) {}
    } u;

    ResourceAddress() = default;

    explicit constexpr ResourceAddress(std::nullptr_t n)
      :type(RESOURCE_ADDRESS_NONE), u(n) {}

    explicit constexpr ResourceAddress(enum resource_address_type _type)
      :type(_type), u(nullptr) {}

    explicit constexpr ResourceAddress(const struct file_address &file)
      :type(RESOURCE_ADDRESS_LOCAL), u(file) {}

    constexpr ResourceAddress(enum resource_address_type _type,
                              const struct http_address &http)
      :type(_type), u(http) {}

    explicit constexpr ResourceAddress(const LhttpAddress &lhttp)
      :type(RESOURCE_ADDRESS_LHTTP), u(lhttp) {}

    constexpr ResourceAddress(enum resource_address_type _type,
                              const struct cgi_address &cgi)
      :type(_type), u(cgi) {}

    explicit constexpr ResourceAddress(const struct nfs_address &nfs)
      :type(RESOURCE_ADDRESS_NFS), u(nfs) {}

    ResourceAddress(struct pool &pool, const ResourceAddress &src) {
        CopyFrom(pool, src);
    }

    void Clear() {
        type = RESOURCE_ADDRESS_NONE;
    }

    bool Check(GError **error_r) const;

    /**
     * Is this a CGI address, or a similar protocol?
     */
    bool IsCgiAlike() const {
        return type == RESOURCE_ADDRESS_CGI ||
            type == RESOURCE_ADDRESS_FASTCGI ||
            type == RESOURCE_ADDRESS_WAS;
    }

    struct file_address *GetFile() {
        assert(type == RESOURCE_ADDRESS_LOCAL);

        return const_cast<struct file_address *>(u.file);
    }

    struct cgi_address *GetCgi() {
        assert(IsCgiAlike());

        return const_cast<struct cgi_address *>(u.cgi);
    }

    gcc_pure
    bool HasQueryString() const;

    gcc_pure
    bool IsValidBase() const;

    /**
     * Determine the URI path.  May return nullptr if unknown or not
     * applicable.
     */
    gcc_pure
    const char *GetHostAndPort() const;

    /**
     * Determine the URI path.  May return nullptr if unknown or not
     * applicable.
     */
    gcc_pure
    const char *GetUriPath() const;

    /**
     * Generates a string identifying the address.  This can be used as a
     * key in a hash table.
     */
    gcc_pure
    const char *GetId(struct pool &pool) const;

    void CopyFrom(struct pool &pool, const ResourceAddress &src);

    gcc_malloc
    ResourceAddress *Dup(struct pool &pool) const;

    /**
     * Duplicate the #ResourceAddress object, but replace the HTTP/AJP
     * URI path component.
     */
    ResourceAddress *DupWithPath(struct pool &pool, const char *path) const;

    /**
     * Duplicate this #resource_address object, and inserts the query
     * string from the specified URI.  If this resource address does not
     * support a query string, or if the URI does not have one, the
     * original #resource_address pointer is returned.
     */
    gcc_malloc
    const ResourceAddress *DupWithQueryStringFrom(struct pool &pool,
                                                  const char *uri) const;

    /**
     * Duplicate this #resource_address object, and inserts the URI
     * arguments and the path suffix.  If this resource address does not
     * support the operation, the original #resource_address pointer may
     * be returned.
     */
    gcc_malloc
    const ResourceAddress *DupWithArgs(struct pool &pool,
                                       const char *args, size_t args_length,
                                       const char *path, size_t path_length) const;

    /**
     * Check if a "base" URI can be generated automatically from this
     * #resource_address.  This applies when the CGI's PATH_INFO matches
     * the end of the specified URI.
     *
     * @param uri the request URI
     * @return a newly allocated base, or nullptr if that is not possible
     */
    gcc_malloc
    char *AutoBase(struct pool &pool, const char *uri) const;

    /**
     * Duplicate a resource address, but return the base address.
     *
     * @param suffix the suffix to be removed from #src
     * @return nullptr if the suffix does not match, or if this address type
     * cannot have a base address
     */
    gcc_malloc
    ResourceAddress *SaveBase(struct pool &pool, ResourceAddress &dest,
                              const char *suffix) const;

    /**
     * Duplicate a resource address, and append a suffix.
     *
     * Warning: this function does not check for excessive "../"
     * sub-strings.
     *
     * @param suffix the suffix to be addded to #src
     * @return nullptr if this address type cannot have a base address
     */
    gcc_malloc
    ResourceAddress *LoadBase(struct pool &pool, ResourceAddress &dest,
                              const char *suffix) const;

    /**
     * Copies data from #src for storing in the translation cache.
     * 
    * @return true if a #base was given and it was applied
     * successfully
     */
    bool CacheStore(struct pool *pool,
                    const ResourceAddress *src,
                    const char *uri, const char *base,
                    bool easy_base, bool expandable);

    /**
     * Load an address from a cached object, and apply any BASE
     * changes (if a BASE is present).
     */
    bool CacheLoad(struct pool *pool, const ResourceAddress &src,
                   const char *uri, const char *base,
                   bool unsafe_base, bool expandable,
                   GError **error_r);

    gcc_pure
    const ResourceAddress *Apply(struct pool &pool,
                                 const char *relative, size_t relative_length,
                                 ResourceAddress &buffer) const;

    gcc_pure
    const struct strref *RelativeTo(const ResourceAddress &base,
                                    struct strref &buffer) const;

    /**
     * Does this address need to be expanded with Expand()?
     */
    gcc_pure
    bool IsExpandable() const;

    /**
     * Expand the expand_path_info attribute.
     */
    bool Expand(struct pool &pool, const MatchInfo &match_info, Error &error);
};

#endif
