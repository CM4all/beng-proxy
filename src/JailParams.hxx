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

struct jail_params {
    bool enabled;
    const char *account_id;
    const char *site_id;
    const char *user_name;
    const char *host_name;
    const char *home_directory;

    jail_params() = default;
    jail_params(struct pool *pool, const jail_params &src);
};

void
jail_params_init(struct jail_params *jail);

bool
jail_params_check(const struct jail_params *jail, GError **error_r);

void
jail_params_copy(struct pool *pool, struct jail_params *dest,
                 const struct jail_params *src);

char *
jail_params_id(const struct jail_params *params, char *p);

void
jail_wrapper_insert(Exec &e, const struct jail_params *params,
                    const char *document_root);

#endif
