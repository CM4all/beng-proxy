/*
 * JailCGI integration.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "JailParams.hxx"
#include "exec.hxx"
#include "pool.hxx"
#include "util/CharUtil.hxx"

#include <glib.h>

#include <string.h>

gcc_const
static GQuark
jail_quark(void)
{
    return g_quark_from_static_string("jail");
}

void
jail_params_init(struct jail_params *jail)
{
    memset(jail, 0, sizeof(*jail));
}

bool
jail_params_check(const struct jail_params *jail, GError **error_r)
{
    if (!jail->enabled)
        return true;

    if (jail->home_directory == nullptr) {
        g_set_error(error_r, jail_quark(), 0, "No JailCGI home directory");
        return false;
    }

    return true;
}

jail_params::jail_params(struct pool *pool, const jail_params &src)
    :enabled(src.enabled),
     account_id(p_strdup_checked(pool, src.account_id)),
     site_id(p_strdup_checked(pool, src.site_id)),
     user_name(p_strdup_checked(pool, src.user_name)),
     host_name(p_strdup_checked(pool, src.host_name)),
     home_directory(p_strdup_checked(pool, src.home_directory))
{
}

void
jail_params_copy(struct pool *pool, struct jail_params *dest,
                 const struct jail_params *src)
{
    dest->enabled = src->enabled;
    dest->account_id = p_strdup_checked(pool, src->account_id);
    dest->site_id = p_strdup_checked(pool, src->site_id);
    dest->user_name = p_strdup_checked(pool, src->user_name);
    dest->host_name = p_strdup_checked(pool, src->host_name);
    dest->home_directory = p_strdup_checked(pool, src->home_directory);
}

char *
jail_params_id(const struct jail_params *params, char *p)
{
    if (params->enabled) {
        p = (char *)mempcpy(p, ";j=", 2);
        p = stpcpy(p, params->home_directory);
    }

    return p;
}

void
jail_wrapper_insert(Exec &e, const struct jail_params *params,
                    const char *document_root)
{
    if (params == nullptr || !params->enabled)
        return;

    e.Append("/usr/lib/cm4all/jailcgi/bin/wrapper");

    if (document_root != nullptr) {
        e.Append("-d");
        e.Append(document_root);
    }

    if (params->account_id != nullptr) {
        e.Append("--account");
        e.Append(params->account_id);
    }

    if (params->site_id != nullptr) {
        e.Append("--site");
        e.Append(params->site_id);
    }

    if (params->user_name != nullptr) {
        e.Append("--name");
        e.Append(params->user_name);
    }

    if (params->host_name != nullptr)
        e.SetEnv("JAILCGI_SERVERNAME", params->host_name);

    if (params->home_directory != nullptr) {
        e.Append("--home");
        e.Append(params->home_directory);
    }

    e.Append("--");
}
