/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_FILE_ADDRESS_HXX
#define BENG_PROXY_FILE_ADDRESS_HXX

#include "spawn/ChildOptions.hxx"
#include "util/ConstBuffer.hxx"

#include <inline/compiler.h>

struct pool;
class MatchInfo;
class Error;
struct DelegateAddress;

/**
 * The address of a local static file.
 */
struct FileAddress {
    const char *path;
    const char *deflated = nullptr;
    const char *gzipped = nullptr;

    const char *content_type = nullptr;

    ConstBuffer<void> content_type_lookup = nullptr;

    const char *document_root = nullptr;

    /**
     * The value of #TRANSLATE_EXPAND_PATH.  Only used by the
     * translation cache.
     */
    const char *expand_path = nullptr;

    /**
     * The value of #TRANSLATE_EXPAND_DOCUMENT_ROOT.  Only used by the
     * translation cache.
     */
    const char *expand_document_root = nullptr;

    DelegateAddress *delegate = nullptr;

    bool auto_gzipped = false;

    FileAddress(const char *path);
    FileAddress(struct pool *pool, const FileAddress &src);

    gcc_pure
    bool HasQueryString() const {
        return false;
    }

    bool Check(GError **error_r) const;

    gcc_pure
    bool IsValidBase() const;

    FileAddress *SaveBase(struct pool *pool, const char *suffix) const;
    FileAddress *LoadBase(struct pool *pool, const char *suffix) const;

    /**
     * Does this address need to be expanded with Expand()?
     */
    gcc_pure
    bool IsExpandable() const;

    bool Expand(struct pool *pool, const MatchInfo &match_info,
                Error &error_r);
};

FileAddress *
file_address_new(struct pool &pool, const char *path);

FileAddress *
file_address_dup(struct pool &pool, const FileAddress *src);

#endif
