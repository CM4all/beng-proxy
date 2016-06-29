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
struct StringView;
struct FileAddress;
struct LhttpAddress;
struct HttpAddress;
struct CgiAddress;
struct NfsAddress;
class MatchInfo;
class Error;

struct ResourceAddress {
    enum class Type {
        NONE,
        LOCAL,

        /**
         * A #HttpAddress, which may specify HTTP or AJP.
         */
        HTTP,

        LHTTP,
        PIPE,
        CGI,
        FASTCGI,
        WAS,
        NFS,
    };

    Type type;

private:
    union U {
        const FileAddress *file;

        const HttpAddress *http;

        const LhttpAddress *lhttp;

        const CgiAddress *cgi;

        const NfsAddress *nfs;

        U() = default;
        constexpr U(std::nullptr_t n):file(n) {}
        constexpr U(const FileAddress &_file):file(&_file) {}
        constexpr U(const HttpAddress &_http):http(&_http) {}
        constexpr U(const LhttpAddress &_lhttp):lhttp(&_lhttp) {}
        constexpr U(const CgiAddress &_cgi):cgi(&_cgi) {}
        constexpr U(const NfsAddress &_nfs):nfs(&_nfs) {}
    } u;

public:
    ResourceAddress() = default;

    constexpr ResourceAddress(std::nullptr_t n)
      :type(Type::NONE), u(n) {}

    constexpr ResourceAddress(const FileAddress &file)
      :type(Type::LOCAL), u(file) {}

    constexpr ResourceAddress(const HttpAddress &http)
        :type(Type::HTTP), u(http) {}

    constexpr ResourceAddress(const LhttpAddress &lhttp)
      :type(Type::LHTTP), u(lhttp) {}

    constexpr ResourceAddress(Type _type,
                              const CgiAddress &cgi)
      :type(_type), u(cgi) {}

    constexpr ResourceAddress(const NfsAddress &nfs)
      :type(Type::NFS), u(nfs) {}

    ResourceAddress(struct pool &pool, const ResourceAddress &src) {
        CopyFrom(pool, src);
    }

    constexpr bool IsDefined() const {
        return type != Type::NONE;
    }

    void Clear() {
        type = Type::NONE;
    }

    bool Check(GError **error_r) const;

    gcc_pure
    bool IsHttp() const;

    gcc_pure
    bool IsAnyHttp() const {
        return IsHttp() || type == Type::LHTTP;
    }

    /**
     * Is this a CGI address, or a similar protocol?
     */
    bool IsCgiAlike() const {
        return type == Type::CGI || type == Type::FASTCGI || type == Type::WAS;
    }

    gcc_pure
    const FileAddress &GetFile() const {
        assert(type == Type::LOCAL);

        return *u.file;
    }

    gcc_pure
    FileAddress &GetFile() {
        assert(type == Type::LOCAL);

        return *const_cast<FileAddress *>(u.file);
    }

    gcc_pure
    const HttpAddress &GetHttp() const {
        assert(type == Type::HTTP);

        return *u.http;
    }

    gcc_pure
    HttpAddress &GetHttp() {
        assert(type == Type::HTTP);

        return *const_cast<HttpAddress *>(u.http);
    }

    gcc_pure
    const LhttpAddress &GetLhttp() const {
        assert(type == Type::LHTTP);

        return *u.lhttp;
    }

    gcc_pure
    LhttpAddress &GetLhttp() {
        assert(type == Type::LHTTP);

        return *const_cast<LhttpAddress *>(u.lhttp);
    }

    gcc_pure
    const NfsAddress &GetNfs() const {
        assert(type == Type::NFS);

        return *u.nfs;
    }

    gcc_pure
    NfsAddress &GetNfs() {
        assert(type == Type::NFS);

        return *const_cast<NfsAddress *>(u.nfs);
    }

    gcc_pure
    const CgiAddress &GetCgi() const {
        assert(IsCgiAlike());

        return *u.cgi;
    }

    gcc_pure
    CgiAddress &GetCgi() {
        assert(IsCgiAlike());

        return *const_cast<CgiAddress *>(u.cgi);
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
     * Construct a copy of this object with a different HTTP/AJP URI
     * path component.
     *
     * This is a shallow copy: no memory is duplicated; the new
     * instance contains pointers to the this instance and to the
     * given path parameter.
     */
    ResourceAddress WithPath(struct pool &pool, const char *path) const;

    /**
     * Construct a copy of this object and insert the query string
     * from the specified URI.  If this resource address does not
     * support a query string, or if the URI does not have one, the
     * unmodified original #ResourceAddress is returned.
     *
     * This is a shallow copy: no memory is duplicated; the new
     * instance contains pointers to the this instance and to the
     * given path parameter.
     */
    ResourceAddress WithQueryStringFrom(struct pool &pool,
                                        const char *uri) const;

    /**
     * Construct a copy of this object and insert the URI
     * arguments and the path suffix.  If this resource address does not
     * support the operation, the original #ResourceAddress pointer may
     * be returned.
     *
     * This is a shallow copy: no memory is duplicated; the new
     * instance contains pointers to the this instance and to the
     * given path parameter.
     */
    ResourceAddress WithArgs(struct pool &pool,
                             StringView args, StringView path) const;

    /**
     * Check if a "base" URI can be generated automatically from this
     * #ResourceAddress.  This applies when the CGI's PATH_INFO matches
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
    bool CacheStore(struct pool &pool, const ResourceAddress &src,
                    const char *uri, const char *base,
                    bool easy_base, bool expandable);

    /**
     * Load an address from a cached object, and apply any BASE
     * changes (if a BASE is present).
     */
    bool CacheLoad(struct pool &pool, const ResourceAddress &src,
                   const char *uri, const char *base,
                   bool unsafe_base, bool expandable,
                   GError **error_r);

    gcc_pure
    const ResourceAddress *Apply(struct pool &pool,
                                 StringView relative,
                                 ResourceAddress &buffer) const;

    gcc_pure
    StringView RelativeTo(const ResourceAddress &base) const;

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
