/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_FILE_ADDRESS_HXX
#define BENG_PROXY_FILE_ADDRESS_HXX

#include "spawn/ChildOptions.hxx"
#include "util/ConstBuffer.hxx"

#include <inline/compiler.h>

class AllocatorPtr;
class MatchInfo;
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

    constexpr FileAddress(const char *_path)
        :path(_path)
    {
    }

    FileAddress(AllocatorPtr alloc, const FileAddress &src);

    FileAddress(const FileAddress &) = delete;
    FileAddress &operator=(const FileAddress &) = delete;

    gcc_pure
    bool HasQueryString() const {
        return false;
    }

    /**
     * Throws std::runtime_error on error.
     */
    void Check() const;

    gcc_pure
    bool IsValidBase() const;

    FileAddress *SaveBase(AllocatorPtr alloc, const char *suffix) const;
    FileAddress *LoadBase(AllocatorPtr alloc, const char *suffix) const;

    /**
     * Does this address need to be expanded with Expand()?
     */
    gcc_pure
    bool IsExpandable() const;

    /**
     * Throws std::runtime_error on error.
     */
    void Expand(struct pool *pool, const MatchInfo &match_info);
};

#endif
