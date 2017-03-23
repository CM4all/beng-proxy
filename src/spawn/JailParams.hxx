/*
 * JailCGI integration.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_JAIL_PARAMS_HXX
#define BENG_PROXY_JAIL_PARAMS_HXX

#include "glibfwd.hxx"

struct pool;
struct PreparedChildProcess;
class AllocatorPtr;
class MatchInfo;

struct JailParams {
    bool enabled = false;

    bool expand_home_directory = false;

    const char *account_id = nullptr;
    const char *site_id = nullptr;
    const char *user_name = nullptr;
    const char *host_name = nullptr;
    const char *home_directory = nullptr;

    JailParams() = default;
    JailParams(struct pool *pool, const JailParams &src);

    /**
     * Throws std::runtime_error on error.
     */
    void Check() const;

    char *MakeId(char *p) const;

    bool InsertWrapper(PreparedChildProcess &p,
                       const char *document_root) const;

    bool IsExpandable() const {
        return expand_home_directory;
    }

    void Expand(AllocatorPtr alloc, const MatchInfo &match_info);
};

#endif
