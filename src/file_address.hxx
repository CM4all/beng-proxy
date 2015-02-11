/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_FILE_ADDRESS_HXX
#define BENG_PROXY_FILE_ADDRESS_HXX

#include "child_options.hxx"

#include <inline/compiler.h>

#include <glib.h>

struct pool;

/**
 * The address of a local static file.
 */
struct file_address {
    const char *path;
    const char *deflated;
    const char *gzipped;

    const char *content_type;
    const char *delegate;
    const char *document_root;

    /**
     * The value of #TRANSLATE_EXPAND_PATH.  Only used by the
     * translation cache.
     */
    const char *expand_path;

    /**
     * The value of #TRANSLATE_EXPAND_DOCUMENT_ROOT.  Only used by the
     * translation cache.
     */
    const char *expand_document_root;

    /**
     * Options for the delegate process.
     */
    ChildOptions child_options;

    bool auto_gzipped;

    file_address(const char *path);
    file_address(struct pool *pool, const file_address &src);

    gcc_pure
    bool HasQueryString() const {
        return false;
    }

    gcc_pure
    bool IsValidBase() const;

    struct file_address *SaveBase(struct pool *pool, const char *suffix) const;
    struct file_address *LoadBase(struct pool *pool, const char *suffix) const;

    /**
     * Does this address need to be expanded with file_address_expand()?
     */
    gcc_pure
    bool IsExpandable() const {
        return expand_path != nullptr ||
            expand_document_root != nullptr;
    }

    bool Expand(struct pool *pool, const GMatchInfo *match_info,
                GError **error_r);
};

struct file_address *
file_address_new(struct pool &pool, const char *path);

struct file_address *
file_address_dup(struct pool &pool, const struct file_address *src);

#endif
