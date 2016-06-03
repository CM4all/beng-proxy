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
                       Session *session, WidgetSession *parent)
    throw(std::bad_alloc)
{
    assert(crash_in_unsafe());

    WidgetSession::Set dest;

    for (const auto &src_ws : src) {
        auto *dest_ws = NewFromPool<WidgetSession>(pool, *pool, src_ws,
                                                   *session, parent);
        dest.insert(*dest_ws);
    }

    return dest;
}

WidgetSession::WidgetSession(Session &_session, WidgetSession *_parent,
                             const char *_id)
    throw(std::bad_alloc)
    :session(_session), parent(_parent),
     id(d_strdup(&session.pool, _id)) {}

WidgetSession::WidgetSession(struct dpool &pool, const WidgetSession &src,
                             Session &_session, WidgetSession *_parent)
    throw(std::bad_alloc)
    :session(_session), parent(_parent),
     id(d_strdup(&pool, src.id)),
     children(widget_session_map_dup(&pool, src.children, &session, this)),
     path_info(d_strdup_checked(&pool, src.path_info)),
     query_string(d_strdup_checked(&pool, src.query_string))
{
}

Session::Session(struct dpool &_pool, SessionId _id, const char *_realm)
    throw(std::bad_alloc)
    :pool(_pool), id(_id),
     expires(Expiry::Touched(SESSION_TTL_NEW)),
     realm(d_strdup(&pool, _realm)),
     cookies(pool)
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
     realm(d_strdup(&pool, src.realm)),
     translate(DupBuffer(&pool, src.translate)),
     site(d_strdup_checked(&pool, src.site)),
     user(d_strdup_checked(&pool, src.user)),
     user_expires(src.user_expires),
     language(d_strdup_checked(&pool, src.language)),
     widgets(widget_session_map_dup(&pool, widgets, this, nullptr)),
     cookies(pool, src.cookies)
{
}

void
session_destroy(Session *session)
{
    DeleteDestroyPool(session->pool, session);
}

/**
 * Calculates the score for purging the session: higher score means
 * more likely to be purged.
 */
unsigned
session_purge_score(const Session *session)
{
    if (session->is_new)
        return 1000;

    if (!session->cookie_received)
        return 50;

    if (session->user == nullptr)
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
Session::ClearSite()
{
    assert(crash_in_unsafe());

    if (site != nullptr) {
        d_free(&pool, site);
        site = nullptr;
    }
}

void
Session::ClearUser()
{
    assert(crash_in_unsafe());

    if (user != nullptr) {
        d_free(&pool, user);
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
Session::SetSite(const char *_site)
{
    assert(crash_in_unsafe());
    assert(_site != nullptr);

    if (site != nullptr && strcmp(site, _site) == 0)
        /* same value as before: no-op */
        return true;

    ClearSite();

    try {
        site = d_strdup(&pool, _site);
        return true;
    } catch (std::bad_alloc) {
        return false;
    }
}

bool
Session::SetUser(const char *_user, unsigned max_age)
{
    assert(crash_in_unsafe());
    assert(_user != nullptr);

    if (user == nullptr || strcmp(user, _user) != 0) {
        ClearUser();

        try {
            user = d_strdup(&pool, _user);
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
hashmap_r_get_widget_session(Session *session, WidgetSession::Set &set,
                             const char *id, bool create)
    throw(std::bad_alloc)
{
    assert(crash_in_unsafe());
    assert(session != nullptr);
    assert(id != nullptr);

    auto i = set.find(id, WidgetSession::Compare());
    if (i != set.end())
        return &*i;

    if (!create)
        return nullptr;

    auto *ws = NewFromPool<WidgetSession>(&session->pool, *session, nullptr,
                                          id);
    set.insert(*ws);
    return ws;
}

WidgetSession *
session_get_widget(Session *session, const char *id, bool create)
try {
    assert(crash_in_unsafe());
    assert(session != nullptr);
    assert(id != nullptr);

    return hashmap_r_get_widget_session(session, session->widgets, id,
                                        create);
} catch (std::bad_alloc) {
    return nullptr;
}

WidgetSession *
widget_session_get_child(WidgetSession *parent,
                         const char *id,
                         bool create)
try {
    assert(crash_in_unsafe());
    assert(parent != nullptr);
    assert(id != nullptr);

    return hashmap_r_get_widget_session(&parent->session, parent->children,
                                        id, create);
} catch (std::bad_alloc) {
    return nullptr;
}

static void
widget_session_free(struct dpool *pool, WidgetSession *ws)
{
    assert(crash_in_unsafe());

    d_free(pool, ws->id);

    if (ws->path_info != nullptr)
        d_free(pool, ws->path_info);

    if (ws->query_string != nullptr)
        d_free(pool, ws->query_string);

    DeleteFromPool(pool, ws);
}

static void
widget_session_clear_map(struct dpool *pool, WidgetSession::Set &set)
{
    assert(crash_in_unsafe());
    assert(pool != nullptr);

    set.clear_and_dispose([pool](WidgetSession *ws){
            widget_session_delete(pool, ws);
        });
}

void
widget_session_delete(struct dpool *pool, WidgetSession *ws)
{
    assert(crash_in_unsafe());
    assert(pool != nullptr);
    assert(ws != nullptr);

    widget_session_clear_map(pool, ws->children);

    widget_session_free(pool, ws);
}

void
session_delete_widgets(Session *session)
{
    assert(crash_in_unsafe());
    assert(session != nullptr);

    widget_session_clear_map(&session->pool, session->widgets);
}

void
Session::Expire(Expiry now)
{
    if (user != nullptr && user_expires.IsExpired(now))
        ClearUser();

    cookies.Expire(now);
}
