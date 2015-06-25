/*
 * JailCGI integration.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_JAIL_PARAMS_HXX
#define BENG_PROXY_JAIL_PARAMS_HXX

#include "glibfwd.hxx"

struct pool;
class Exec;

struct JailParams {
    bool enabled;
    const char *account_id;
    const char *site_id;
    const char *user_name;
    const char *host_name;
    const char *home_directory;

    JailParams() = default;
    JailParams(struct pool *pool, const JailParams &src);

    void Init();

    void CopyFrom(struct pool &pool, const JailParams &src);

    bool Check(GError **error_r) const;

    char *MakeId(char *p) const;

    void InsertWrapper(Exec &e, const char *document_root) const;
};

#endif
