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
JailParams::Init()
{
    memset(this, 0, sizeof(*this));
}

bool
JailParams::Check(GError **error_r) const
{
    if (!enabled)
        return true;

    if (home_directory == nullptr) {
        g_set_error(error_r, jail_quark(), 0, "No JailCGI home directory");
        return false;
    }

    return true;
}

JailParams::JailParams(struct pool *pool, const JailParams &src)
    :enabled(src.enabled),
     account_id(p_strdup_checked(pool, src.account_id)),
     site_id(p_strdup_checked(pool, src.site_id)),
     user_name(p_strdup_checked(pool, src.user_name)),
     host_name(p_strdup_checked(pool, src.host_name)),
     home_directory(p_strdup_checked(pool, src.home_directory))
{
}

void
JailParams::CopyFrom(struct pool &pool, const JailParams &src)
{
    enabled = src.enabled;
    account_id = p_strdup_checked(&pool, src.account_id);
    site_id = p_strdup_checked(&pool, src.site_id);
    user_name = p_strdup_checked(&pool, src.user_name);
    host_name = p_strdup_checked(&pool, src.host_name);
    home_directory = p_strdup_checked(&pool, src.home_directory);
}

char *
JailParams::MakeId(char *p) const
{
    if (enabled) {
        p = (char *)mempcpy(p, ";j=", 3);
        p = stpcpy(p, home_directory);
    }

    return p;
}

void
JailParams::InsertWrapper(Exec &e, const char *document_root) const
{
    if (!enabled)
        return;

    e.Append("/usr/lib/cm4all/jailcgi/bin/wrapper");

    if (document_root != nullptr) {
        e.Append("-d");
        e.Append(document_root);
    }

    if (account_id != nullptr) {
        e.Append("--account");
        e.Append(account_id);
    }

    if (site_id != nullptr) {
        e.Append("--site");
        e.Append(site_id);
    }

    if (user_name != nullptr) {
        e.Append("--name");
        e.Append(user_name);
    }

    if (host_name != nullptr)
        e.SetEnv("JAILCGI_SERVERNAME", host_name);

    if (home_directory != nullptr) {
        e.Append("--home");
        e.Append(home_directory);
    }

    e.Append("--");
}
