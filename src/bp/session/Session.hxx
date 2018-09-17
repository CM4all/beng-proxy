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

/*
 * Session management.
 */

#ifndef BENG_PROXY_SESSION_HXX
#define BENG_PROXY_SESSION_HXX

#include "Id.hxx"
#include "cookie_jar.hxx"
#include "shm/String.hxx"
#include "util/ConstBuffer.hxx"
#include "util/Expiry.hxx"

#include "util/Compiler.h"

#include <boost/intrusive/set.hpp>
#include <boost/intrusive/unordered_set.hpp>
#include <boost/interprocess/sync/interprocess_mutex.hpp>

#include <chrono>

#include <string.h>

struct dpool;
struct RealmSession;
struct HttpAddress;

/**
 * Session data associated with a widget instance (struct widget).
 */
struct WidgetSession
    : boost::intrusive::set_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>> {

    struct Compare {
        bool operator()(const WidgetSession &a, const WidgetSession &b) const {
            return strcmp(a.id, b.id) < 0;
        }

        bool operator()(const WidgetSession &a, const char *b) const {
            return strcmp(a.id, b) < 0;
        }

        bool operator()(const char *a, const WidgetSession &b) const {
            return strcmp(a, b.id) < 0;
        }
    };

    typedef boost::intrusive::set<WidgetSession,
                                  boost::intrusive::compare<Compare>,
                                  boost::intrusive::constant_time_size<false>> Set;

    RealmSession &session;

    /** local id of this widget; must not be nullptr since widgets
        without an id cannot have a session */
    const DString id;

    Set children;

    /** last relative URI */
    DString path_info;

    /** last query string */
    DString query_string;

    /**
     * Throws std::bad_alloc on error.
     */
    WidgetSession(RealmSession &_session, const char *_id);

    /**
     * Throws std::bad_alloc on error.
     */
    WidgetSession(struct dpool &pool, const WidgetSession &src,
                  RealmSession &_session);

    void Destroy(struct dpool &pool);

    gcc_pure
    WidgetSession *GetChild(const char *child_id, bool create);
};

struct Session;

/**
 * A session associated with a user.
 */
struct RealmSession
    : boost::intrusive::set_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>> {

    static constexpr auto link_mode = boost::intrusive::normal_link;
    typedef boost::intrusive::link_mode<link_mode> LinkMode;
    typedef boost::intrusive::unordered_set_member_hook<LinkMode> SetHook;
    SetHook set_hook;

    Session &parent;

    struct Compare {
        bool operator()(const RealmSession &a, const RealmSession &b) const {
            return strcmp(a.realm, b.realm) < 0;
        }

        bool operator()(const RealmSession &a, const char *b) const {
            return strcmp(a.realm, b) < 0;
        }

        bool operator()(const char *a, const RealmSession &b) const {
            return strcmp(a, b.realm) < 0;
        }
    };

    /**
     * The name of this session's realm.  It is always non-nullptr.
     */
    const DString realm;

    /**
     * The site name as provided by the translation server in the
     * packet #TRANSLATE_SESSION_SITE.
     */
    DString site;

    /** the user name which is logged in (nullptr if anonymous), provided
        by the translation server */
    DString user;

    /** when will the #user attribute expire? */
    Expiry user_expires = Expiry::Never();

    /** a map of widget path to WidgetSession */
    WidgetSession::Set widgets;

    /** all cookies received by widget servers */
    CookieJar cookies;

    /**
     * Throws std::bad_alloc on error.
     */
    RealmSession(Session &_parent, const char *realm);

    /**
     * Throws std::bad_alloc on error.
     */
    RealmSession(Session &_parent, const RealmSession &src);

    void ClearSite();
    bool SetSite(const char *_site);

    /**
     * @param max_age 0 = expires immediately; negative = never
     * expires.
     */
    bool SetUser(const char *user, std::chrono::seconds max_age);
    void ClearUser();

    void Expire(Expiry now);

    gcc_pure
    WidgetSession *GetWidget(const char *widget_id, bool create);
};

struct Session {
    static constexpr auto link_mode = boost::intrusive::normal_link;
    typedef boost::intrusive::link_mode<link_mode> LinkMode;
    typedef boost::intrusive::unordered_set_member_hook<LinkMode> SetHook;
    SetHook set_hook;

    struct dpool &pool;

    /** this lock protects the bit fields, all widget session hash
        maps and the cookie jar */
    boost::interprocess::interprocess_mutex mutex;

    /** identification number of this session */
    const SessionId id;

    /** when will this session expire? */
    Expiry expires;

    /**
     * Counts how often this session has been used.
     */
    unsigned counter = 1;

    /** is this a new session, i.e. there hasn't been a second request
        yet? */
    bool is_new = true;

    /** has a HTTP cookie with this session id already been sent? */
    bool cookie_sent = false;

    /** has a HTTP cookie with this session id already been received? */
    bool cookie_received = false;

    /** an opaque string for the translation server */
    ConstBuffer<void> translate = nullptr;

    /** optional  for the "Accept-Language" header, provided
        by the translation server */
    DString language;

    /** @see #TRANSLATE_EXTERNAL_SESSION_MANAGER */
    HttpAddress *external_manager = nullptr;

    /** @see #TRANSLATE_EXTERNAL_SESSION_KEEPALIVE */
    std::chrono::duration<uint16_t> external_keepalive;

    std::chrono::steady_clock::time_point next_external_keepalive;

    typedef boost::intrusive::set<RealmSession,
                                  boost::intrusive::compare<RealmSession::Compare>,
                                  boost::intrusive::constant_time_size<false>> RealmSessionSet;

    RealmSessionSet realms;

    Session(struct dpool &_pool, SessionId _id);

    /**
     * Throws std::bad_alloc on error.
     */
    Session(struct dpool &_pool, const Session &src);

    void Destroy();

    /**
     * Calculates the score for purging the session: higher score
     * means more likely to be purged.
     */
    gcc_pure
    unsigned GetPurgeScore() const noexcept;

    gcc_pure
    bool HasUser() const {
        for (auto &realm : realms)
            if (realm.user != nullptr)
                return true;

        return false;
    }

    bool SetTranslate(ConstBuffer<void> translate);
    void ClearTranslate();

    bool SetLanguage(const char *language);
    void ClearLanguage();

    bool SetExternalManager(const HttpAddress &address,
                            std::chrono::steady_clock::time_point now,
                            std::chrono::duration<uint16_t> keepalive);

    void Expire(Expiry now);

    gcc_pure
    RealmSession *GetRealm(const char *realm);

    struct Disposer {
        void operator()(Session *session) {
            session->Destroy();
        }
    };
};

/**
 * Finds the session with the specified id.  The returned object is
 * locked, and must be unlocked with session_put().
 */
Session *
session_get(SessionId id);

/**
 * Unlocks the specified session.
 */
void
session_put(Session *session);

static inline void
session_put(RealmSession &session)
{
    session_put(&session.parent);
}

static inline void
session_put(RealmSession *session)
{
    session_put(&session->parent);
}

inline RealmSession *
session_get_realm(SessionId id, const char *realm)
{
    auto *session = session_get(id);
    if (session == nullptr)
        return nullptr;

    auto *realm_session = session->GetRealm(realm);
    if (realm_session == nullptr)
        session_put(session);

    return realm_session;
}

/**
 * Deletes the session with the specified id.  The current process
 * must not hold a sssion lock.
 */
void
session_delete(SessionId id);

class SessionLease {
    friend class RealmSessionLease;

    Session *session;

public:
    SessionLease():session(nullptr) {}
    SessionLease(std::nullptr_t):session(nullptr) {}

    explicit SessionLease(SessionId id)
        :session(session_get(id)) {}

    explicit SessionLease(Session *_session)
        :session(_session) {}

    SessionLease(SessionLease &&src)
        :session(src.session) {
        src.session = nullptr;
    }

    ~SessionLease() {
        if (session != nullptr)
            session_put(session);
    }

    SessionLease &operator=(SessionLease &&src) {
        std::swap(session, src.session);
        return *this;
    }

    operator bool() const {
        return session != nullptr;
    }

    Session &operator *() {
        return *session;
    }

    Session *operator->() {
        return session;
    }

    Session *get() {
        return session;
    }
};

class RealmSessionLease {
    RealmSession *session;

public:
    RealmSessionLease():session(nullptr) {}
    RealmSessionLease(std::nullptr_t):session(nullptr) {}

    RealmSessionLease(SessionLease &&src, const char *realm)
        :session(src.session != nullptr
                 ? src.session->GetRealm(realm)
                 : nullptr) {
        if (session != nullptr)
            src.session = nullptr;
    }

    RealmSessionLease(SessionId id, const char *realm)
        :session(nullptr) {
        SessionLease parent(id);
        if (!parent)
            return;

        session = parent.session->GetRealm(realm);
        if (session != nullptr)
            parent.session = nullptr;
    }

    explicit RealmSessionLease(RealmSession *_session)
        :session(_session) {}

    RealmSessionLease(RealmSessionLease &&src)
        :session(src.session) {
        src.session = nullptr;
    }

    ~RealmSessionLease() {
        if (session != nullptr)
            session_put(&session->parent);
    }

    RealmSessionLease &operator=(RealmSessionLease &&src) {
        std::swap(session, src.session);
        return *this;
    }

    operator bool() const {
        return session != nullptr;
    }

    RealmSession &operator *() {
        return *session;
    }

    RealmSession *operator->() {
        return session;
    }

    RealmSession *get() {
        return session;
    }
};

#endif
