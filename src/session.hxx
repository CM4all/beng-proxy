/*
 * Session management.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SESSION_HXX
#define BENG_PROXY_SESSION_HXX

#include "shm/lock.h"
#include "session_id.hxx"
#include "util/ConstBuffer.hxx"

#include <inline/compiler.h>

#include <boost/intrusive/set.hpp>
#include <boost/intrusive/unordered_set.hpp>

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

    WidgetSession *parent;

    Session *session;

    /** local id of this widget; must not be nullptr since widgets
        without an id cannot have a session */
    const char *id;

    Set children;

    /** last relative URI */
    char *path_info;

    /** last query string */
    char *query_string;
};

/**
 * A session associated with a user.
 */
struct Session {
    static constexpr auto link_mode = boost::intrusive::normal_link;
    typedef boost::intrusive::link_mode<link_mode> LinkMode;
    typedef boost::intrusive::unordered_set_member_hook<LinkMode> SetHook;
    SetHook set_hook;

    struct dpool *pool;

    /** this lock protects the bit fields, all widget session hash
        maps and the cookie jar */
    struct lock lock;

    /** identification number of this session */
    SessionId id;

    /** when will this session expire? */
    time_t expires;

    /**
     * Counts how often this session has been used.
     */
    unsigned counter;

    /** is this a new session, i.e. there hasn't been a second request
        yet? */
    bool is_new;

    /** has a HTTP cookie with this session id already been sent? */
    bool cookie_sent;

    /** has a HTTP cookie with this session id already been received? */
    bool cookie_received;

    /**
     * The name of this session's realm.  It is always non-nullptr.
     */
    const char *realm;

    /** an opaque string for the translation server */
    ConstBuffer<void> translate;

    /** the user name which is logged in (nullptr if anonymous), provided
        by the translation server */
    const char *user;

    /** when will the #user attribute expire? */
    time_t user_expires;

    /** optional  for the "Accept-Language" header, provided
        by the translation server */
    const char *language;

    /** a map of widget path to WidgetSession */
    WidgetSession::Set widgets;

    /** all cookies received by widget servers */
    struct cookie_jar *cookies;

    Session(struct dpool *_pool, const char *realm);
    Session(struct dpool *_pool, const Session &src);
    ~Session();
};

gcc_malloc
Session *
session_allocate(struct dpool *pool, const char *realm);

gcc_malloc
Session *
session_dup(struct dpool *pool, const Session *src);

void
session_destroy(Session *session);

gcc_pure
unsigned
session_purge_score(const Session *session);

void
session_clear_translate(Session *session);

void
session_clear_user(Session *session);

void
session_clear_language(Session *session);

bool
session_set_translate(Session *session, ConstBuffer<void> translate);

bool
session_set_user(Session *session, const char *user, unsigned max_age);

bool
session_set_language(Session *session, const char *language);

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

gcc_malloc
WidgetSession *
widget_session_allocate(Session *session);

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
