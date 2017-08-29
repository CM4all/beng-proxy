/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "session.hxx"
#include "cookie_jar.hxx"
#include "http_address.hxx"
#include "shm/dpool.hxx"
#include "shm/dbuffer.hxx"
#include "crash.hxx"

#include <assert.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

static constexpr std::chrono::seconds SESSION_TTL_NEW(120);

static WidgetSession::Set
widget_session_map_dup(struct dpool &pool, const WidgetSession::Set &src,
                       RealmSession *session)
    throw(std::bad_alloc)
{
    assert(crash_in_unsafe());

    WidgetSession::Set dest;

    for (const auto &src_ws : src) {
        auto *dest_ws = NewFromPool<WidgetSession>(pool, pool, src_ws,
                                                   *session);
        dest.insert(*dest_ws);
    }

    return dest;
}

WidgetSession::WidgetSession(RealmSession &_session,  const char *_id)
    throw(std::bad_alloc)
    :session(_session),
     id(session.parent.pool, _id) {}

WidgetSession::WidgetSession(struct dpool &pool, const WidgetSession &src,
                             RealmSession &_session)
    throw(std::bad_alloc)
    :session(_session),
     id(pool, src.id),
     children(widget_session_map_dup(pool, src.children, &session)),
     path_info(pool, src.path_info),
     query_string(pool, src.query_string)
{
}

RealmSession::RealmSession(Session &_parent, const char *_realm)
    throw(std::bad_alloc)
    :parent(_parent),
     realm(parent.pool, _realm),
     cookies(parent.pool)
{
}

RealmSession::RealmSession(Session &_parent, const RealmSession &src)
    throw(std::bad_alloc)
    :parent(_parent),
     realm(parent.pool, src.realm),
     site(parent.pool, src.site),
     user(parent.pool, src.user),
     user_expires(src.user_expires),
     widgets(widget_session_map_dup(parent.pool, widgets, this)),
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
     translate(DupBuffer(pool, src.translate)),
     language(pool, src.language),
     external_manager(src.external_manager != nullptr
                      ? NewFromPool<HttpAddress>(pool, pool,
                                                 *src.external_manager)
                      : nullptr),
     external_keepalive(src.external_keepalive)
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
        d_free(pool, translate.data);
        translate = nullptr;
    }
}

void
RealmSession::ClearSite()
{
    assert(crash_in_unsafe());

    site.Clear(parent.pool);
}

void
RealmSession::ClearUser()
{
    assert(crash_in_unsafe());

    user.Clear(parent.pool);
}

void
Session::ClearLanguage()
{
    assert(crash_in_unsafe());

    language.Clear(pool);
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
        translate = DupBuffer(pool, _translate);
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

    return site.SetNoExcept(parent.pool, _site);
}

bool
RealmSession::SetUser(const char *_user, std::chrono::seconds max_age)
{
    assert(crash_in_unsafe());
    assert(_user != nullptr);

    if (!user.SetNoExcept(parent.pool, _user))
        return false;

    if (max_age < std::chrono::seconds::zero())
        /* never expires */
        user_expires = Expiry::Never();
    else if (max_age == std::chrono::seconds::zero())
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

    return language.SetNoExcept(pool, _language);
}

bool
Session::SetExternalManager(const HttpAddress &address,
                            std::chrono::duration<uint16_t> keepalive)
{
    assert(crash_in_unsafe());

    if (external_manager != nullptr) {
        external_manager->Free(pool);
        DeleteFromPool(pool, external_manager);
        external_manager = nullptr;
    } else {
        next_external_keepalive = std::chrono::steady_clock::time_point::min();
    }

    try {
        external_manager = NewFromPool<HttpAddress>(pool, pool, address);
        external_keepalive = keepalive;

        /* assume the session is fresh now; postpone the first refresh
           for one period */
        next_external_keepalive = std::chrono::steady_clock::now() + keepalive;

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

    auto *ws = NewFromPool<WidgetSession>(session.parent.pool, session, id);
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

    d_free(pool, id);

    if (path_info != nullptr)
        d_free(pool, path_info);

    if (query_string != nullptr)
        d_free(pool, query_string);

    DeleteFromPool(pool, this);
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

    auto realm = NewFromPool<RealmSession>(pool, *this, realm_name);
    realms.insert_commit(*realm, commit_data);
    return realm;
} catch (std::bad_alloc) {
    return nullptr;
}
