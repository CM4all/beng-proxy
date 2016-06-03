/*
 * Session management.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SESSION_HXX
#define BENG_PROXY_SESSION_HXX

#include "session_id.hxx"
#include "expiry.hxx"
#include "cookie_jar.hxx"
#include "util/ConstBuffer.hxx"

#include <inline/compiler.h>

#include <boost/intrusive/set.hpp>
#include <boost/intrusive/unordered_set.hpp>
#include <boost/interprocess/sync/interprocess_mutex.hpp>

#include <string.h>
#include <time.h>

struct dpool;
struct Session;

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

    Session &session;

    /** local id of this widget; must not be nullptr since widgets
        without an id cannot have a session */
    const char *const id;

    Set children;

    /** last relative URI */
    char *path_info = nullptr;

    /** last query string */
    char *query_string = nullptr;

    WidgetSession(Session &_session, const char *_id)
        throw(std::bad_alloc);

    WidgetSession(struct dpool &pool, const WidgetSession &src,
                  Session &_session)
        throw(std::bad_alloc);
};

/**
 * A session associated with a user.
 */
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

    /**
     * The name of this session's realm.  It is always non-nullptr.
     */
    const char *const realm;

    /** an opaque string for the translation server */
    ConstBuffer<void> translate = nullptr;

    /**
     * The site name as provided by the translation server in the
     * packet #TRANSLATE_SESSION_SITE.
     */
    const char *site = nullptr;

    /** the user name which is logged in (nullptr if anonymous), provided
        by the translation server */
    const char *user = nullptr;

    /** when will the #user attribute expire? */
    Expiry user_expires = Expiry::Never();

    /** optional  for the "Accept-Language" header, provided
        by the translation server */
    const char *language = nullptr;

    /** a map of widget path to WidgetSession */
    WidgetSession::Set widgets;

    /** all cookies received by widget servers */
    CookieJar cookies;

    Session(struct dpool &_pool, SessionId _id, const char *realm)
        throw(std::bad_alloc);

    Session(struct dpool &_pool, const Session &src)
        throw(std::bad_alloc);

    void ClearSite();
    bool SetSite(const char *_site);

    bool SetTranslate(ConstBuffer<void> translate);
    void ClearTranslate();

    bool SetUser(const char *user, unsigned max_age);
    void ClearUser();

    bool SetLanguage(const char *language);
    void ClearLanguage();

    void Expire(Expiry now);
};

void
session_destroy(Session *session);

gcc_pure
unsigned
session_purge_score(const Session *session);

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

/**
 * Deletes the session with the specified id.  The current process
 * must not hold a sssion lock.
 */
void
session_delete(SessionId id);

gcc_pure
WidgetSession *
session_get_widget(Session *session, const char *id, bool create);

gcc_pure
WidgetSession *
widget_session_get_child(WidgetSession *parent, const char *id,
                         bool create);

void
widget_session_delete(struct dpool *pool, WidgetSession *ws);

void
session_delete_widgets(Session *session);

#endif
