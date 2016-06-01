/*
 * Session management.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "session.hxx"
#include "cookie_jar.hxx"
#include "shm/dpool.hxx"
#include "shm/dbuffer.hxx"
#include "crash.hxx"

#include <daemon/log.h>

#include <assert.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#define SESSION_TTL_NEW 120

static WidgetSession::Set
widget_session_map_dup(struct dpool *pool, const WidgetSession::Set &src,
                       RealmSession *session)
    throw(std::bad_alloc)
{
    assert(crash_in_unsafe());

    WidgetSession::Set dest;

    for (const auto &src_ws : src) {
        auto *dest_ws = NewFromPool<WidgetSession>(pool, *pool, src_ws,
                                                   *session);
        dest.insert(*dest_ws);
    }

    return dest;
}

WidgetSession::WidgetSession(RealmSession &_session,  const char *_id)
    throw(std::bad_alloc)
    :session(_session),
     id(d_strdup(&session.parent.pool, _id)) {}

WidgetSession::WidgetSession(struct dpool &pool, const WidgetSession &src,
                             RealmSession &_session)
    throw(std::bad_alloc)
    :session(_session),
     id(d_strdup(&pool, src.id)),
     children(widget_session_map_dup(&pool, src.children, &session)),
     path_info(d_strdup_checked(&pool, src.path_info)),
     query_string(d_strdup_checked(&pool, src.query_string))
{
}

RealmSession::RealmSession(Session &_parent, const char *_realm)
    throw(std::bad_alloc)
    :parent(_parent),
     realm(d_strdup(&parent.pool, _realm)),
     cookies(parent.pool)
{
}

RealmSession::RealmSession(Session &_parent, const RealmSession &src)
    throw(std::bad_alloc)
    :parent(_parent),
     realm(d_strdup(&parent.pool, src.realm)),
     site(d_strdup_checked(&parent.pool, src.site)),
     user(d_strdup_checked(&parent.pool, src.user)),
     user_expires(src.user_expires),
     widgets(widget_session_map_dup(&parent.pool, widgets, this)),
     cookies(parent.pool, src.cookies)
{
}

Session::Session(struct dpool &_pool, SessionId _id)
    :pool(_pool),
     id(_id),
     expires(Expiry::Touched(SESSION_TTL_NEW))
{
}

Session::Session(struct dpool &_pool, const Session &src)
    throw(std::bad_alloc)
    :pool(_pool),
     id(src.id),
     expires(src.expires),
     counter(src.counter),
     is_new(src.is_new),
     cookie_sent(src.cookie_sent), cookie_received(src.cookie_received),
     translate(DupBuffer(&pool, src.translate)),
     language(d_strdup_checked(&pool, src.language))
{
}

void
Session::Destroy()
{
    DeleteDestroyPool(pool, this);
}

unsigned
Session::GetPurgeScore() const noexcept
{
    if (is_new)
        return 1000;

    if (!cookie_received)
        return 50;

    if (!HasUser())
        return 20;

    return 1;
}

void
Session::ClearTranslate()
{
    assert(crash_in_unsafe());

    if (!translate.IsEmpty()) {
        d_free(&pool, translate.data);
        translate = nullptr;
    }
}

void
RealmSession::ClearSite()
{
    assert(crash_in_unsafe());

    if (site != nullptr) {
        d_free(&parent.pool, site);
        site = nullptr;
    }
}

void
RealmSession::ClearUser()
{
    assert(crash_in_unsafe());

    if (user != nullptr) {
        d_free(&parent.pool, user);
        user = nullptr;
    }
}

void
Session::ClearLanguage()
{
    assert(crash_in_unsafe());

    if (language != nullptr) {
        d_free(&pool, language);
        language = nullptr;
    }
}

bool
Session::SetTranslate(ConstBuffer<void> _translate)
{
    assert(crash_in_unsafe());
    assert(!_translate.IsNull());

    if (!translate.IsNull() &&
        translate.size == _translate.size &&
        memcmp(translate.data, _translate.data, _translate.size) == 0)
        /* same value as before: no-op */
        return true;

    ClearTranslate();

    try {
        translate = DupBuffer(&pool, _translate);
        return true;
    } catch (std::bad_alloc) {
        return false;
    }
}

bool
RealmSession::SetSite(const char *_site)
{
    assert(crash_in_unsafe());
    assert(_site != nullptr);

    if (site != nullptr && strcmp(site, _site) == 0)
        /* same value as before: no-op */
        return true;

    ClearSite();

    try {
        site = d_strdup(&parent.pool, _site);
        return true;
    } catch (std::bad_alloc) {
        return false;
    }
}

bool
RealmSession::SetUser(const char *_user, unsigned max_age)
{
    assert(crash_in_unsafe());
    assert(_user != nullptr);

    if (user == nullptr || strcmp(user, _user) != 0) {
        ClearUser();

        try {
            user = d_strdup(&parent.pool, _user);
        } catch (std::bad_alloc) {
            return false;
        }
    }

    if (max_age == (unsigned)-1)
        /* never expires */
        user_expires = Expiry::Never();
    else if (max_age == 0)
        /* expires immediately, use only once */
        user_expires = Expiry::AlreadyExpired();
    else
        user_expires.Touch(max_age);

    return true;
}

bool
Session::SetLanguage(const char *_language)
{
    assert(crash_in_unsafe());
    assert(_language != nullptr);

    if (language != nullptr && strcmp(language, _language) == 0)
        /* same value as before: no-op */
        return true;

    ClearLanguage();

    try {
        language = d_strdup(&pool, _language);
        return true;
    } catch (std::bad_alloc) {
        return false;
    }
}

static WidgetSession *
hashmap_r_get_widget_session(RealmSession &session, WidgetSession::Set &set,
                             const char *id, bool create)
    throw(std::bad_alloc)
{
    assert(crash_in_unsafe());
    assert(id != nullptr);

    auto i = set.find(id, WidgetSession::Compare());
    if (i != set.end())
        return &*i;

    if (!create)
        return nullptr;

    auto *ws = NewFromPool<WidgetSession>(&session.parent.pool, session, id);
    set.insert(*ws);
    return ws;
}

WidgetSession *
RealmSession::GetWidget(const char *widget_id, bool create)
try {
    assert(crash_in_unsafe());
    assert(widget_id != nullptr);

    return hashmap_r_get_widget_session(*this, widgets, widget_id, create);
} catch (std::bad_alloc) {
    return nullptr;
}

WidgetSession *
WidgetSession::GetChild(const char *child_id, bool create)
try {
    assert(crash_in_unsafe());
    assert(child_id != nullptr);

    return hashmap_r_get_widget_session(session, children, child_id, create);
} catch (std::bad_alloc) {
    return nullptr;
}

void
WidgetSession::Destroy(struct dpool &pool)
{
    assert(crash_in_unsafe());

    children.clear_and_dispose([&pool](WidgetSession *ws){
            ws->Destroy(pool);
        });

    d_free(&pool, id);

    if (path_info != nullptr)
        d_free(&pool, path_info);

    if (query_string != nullptr)
        d_free(&pool, query_string);

    DeleteFromPool(&pool, this);
}

void
RealmSession::Expire(Expiry now)
{
    if (user != nullptr && user_expires.IsExpired(now))
        ClearUser();

    cookies.Expire(now);
}

void
Session::Expire(Expiry now)
{
    for (auto &realm : realms)
        realm.Expire(now);
}

RealmSession *
Session::GetRealm(const char *realm_name)
try {
    RealmSessionSet::insert_commit_data commit_data;
    auto result = realms.insert_check(realm_name, RealmSession::Compare(),
                                      commit_data);
    if (!result.second)
        return &*result.first;

    auto realm = NewFromPool<RealmSession>(&pool, *this, realm_name);
    realms.insert_commit(*realm, commit_data);
    return realm;
} catch (std::bad_alloc) {
    return nullptr;
}
