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
};

void
jail_params_init(JailParams *jail);

bool
jail_params_check(const JailParams *jail, GError **error_r);

void
jail_params_copy(struct pool *pool, JailParams *dest,
                 const JailParams *src);

char *
jail_params_id(const JailParams *params, char *p);

void
jail_wrapper_insert(Exec &e, const JailParams *params,
                    const char *document_root);

#endif
