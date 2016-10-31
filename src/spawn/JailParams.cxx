/*
 * JailCGI integration.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "JailParams.hxx"
#include "Prepared.hxx"
#include "pool.hxx"
#include "pexpand.hxx"
#include "util/CharUtil.hxx"
#include "util/ConstBuffer.hxx"

#include <stdexcept>

#include <string.h>

void
JailParams::Check() const
{
    if (!enabled)
        return;

    if (home_directory == nullptr)
        throw std::runtime_error("No JailCGI home directory");
}

JailParams::JailParams(struct pool *pool, const JailParams &src)
    :enabled(src.enabled),
     expand_home_directory(src.expand_home_directory),
     account_id(p_strdup_checked(pool, src.account_id)),
     site_id(p_strdup_checked(pool, src.site_id)),
     user_name(p_strdup_checked(pool, src.user_name)),
     host_name(p_strdup_checked(pool, src.host_name)),
     home_directory(p_strdup_checked(pool, src.home_directory))
{
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

bool
JailParams::InsertWrapper(PreparedChildProcess &p,
                          const char *document_root) const
{
    if (!enabled)
        return true;

    StaticArray<const char *, 16> w;

    w.push_back("/usr/lib/cm4all/jailcgi/bin/wrapper");

    if (document_root != nullptr) {
        w.push_back("-d");
        w.push_back(document_root);
    }

    if (account_id != nullptr) {
        w.push_back("--account");
        w.push_back(account_id);
    }

    if (site_id != nullptr) {
        w.push_back("--site");
        w.push_back(site_id);
    }

    if (user_name != nullptr) {
        w.push_back("--name");
        w.push_back(user_name);
    }

    if (host_name != nullptr)
        p.SetEnv("JAILCGI_SERVERNAME", host_name);

    if (home_directory != nullptr) {
        w.push_back("--home");
        w.push_back(home_directory);
    }

    w.push_back("--");

    return p.InsertWrapper({w.raw(), w.size()});
}

bool
JailParams::Expand(struct pool &pool, const MatchInfo &match_info,
                   Error &error_r)
{
    if (expand_home_directory) {
        home_directory =
            expand_string_unescaped(&pool, home_directory, match_info,
                                    error_r);
        if (home_directory == nullptr)
            return false;
    }

    return true;
}
