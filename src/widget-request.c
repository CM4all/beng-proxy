/*
 * Copy parameters from a request to the widget object.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "widget-request.h"
#include "widget-quark.h"
#include "widget.h"
#include "widget-class.h"
#include "session.h"
#include "penv.h"
#include "uri-parser.h"
#include "uri-relative.h"
#include "strref-dpool.h"
#include "pool.h"

#include <string.h>
#include <assert.h>

/** copy data from the widget to its associated session */
static void
widget_to_session(struct widget_session *ws, const struct widget *widget)
{
    assert(widget != NULL);
    assert(widget->cls != NULL);
    assert(widget->cls->stateful); /* cannot save state for stateless widgets */

    if (ws->path_info != NULL)
        d_free(ws->session->pool, ws->path_info);

    ws->path_info = widget->from_request.path_info == NULL
        ? NULL
        : d_strdup(ws->session->pool, widget->from_request.path_info);

    if (ws->query_string != NULL)
        d_free(ws->session->pool, ws->query_string);

    ws->query_string = strref_is_empty(&widget->from_request.query_string)
        ? NULL
        : strref_dup_d(ws->session->pool, &widget->from_request.query_string);
}

/** restore data from the session */
static void
session_to_widget(struct widget *widget, const struct widget_session *ws)
{
    assert(widget->cls != NULL);
    assert(widget->cls->stateful); /* cannot load state from stateless widgets */
    assert(widget->lazy.address == NULL);

    widget->from_request.path_info = ws->path_info;

    if (ws->query_string != NULL)
        strref_set_c(&widget->from_request.query_string, ws->query_string);
}

static bool
widget_has_focus(const struct widget *widget)
{
    assert(widget != NULL);
    assert(widget->parent != NULL);

    return widget->id != NULL &&
        widget->parent->from_request.focus_ref != NULL &&
        strcmp(widget->id, widget->parent->from_request.focus_ref->id) == 0 &&
        widget->parent->from_request.focus_ref->next == NULL;
}

static bool
widget_descendant_has_focus(const struct widget *widget)
{
    assert(widget != NULL);
    assert(widget->parent != NULL);

    return widget->id != NULL &&
        widget->parent->from_request.focus_ref != NULL &&
        strcmp(widget->id, widget->parent->from_request.focus_ref->id) == 0 &&
        widget->parent->from_request.focus_ref->next != NULL;
}

bool
widget_copy_from_request(struct widget *widget, struct processor_env *env,
                         GError **error_r)
{
    assert(widget != NULL);
    assert(widget->parent != NULL);
    assert(widget->lazy.address == NULL);
    assert(widget->from_request.path_info == NULL);
    assert(strref_is_empty(&widget->from_request.query_string));
    assert(widget->from_request.focus_ref == NULL);
    assert(widget->from_request.method == HTTP_METHOD_GET);
    assert(widget->from_request.body == NULL);

    if (widget->id == NULL)
        return true;

    /* are we focused? */

    if (widget_has_focus(widget)) {
        /* we're in focus.  forward query string and request body. */
        widget->from_request.path_info = env->path_info;
        if (widget->from_request.path_info != NULL) {
            widget->from_request.path_info =
                uri_compress(env->pool, widget->from_request.path_info);
            if (widget->from_request.path_info == NULL) {
                g_set_error(error_r, widget_quark(), WIDGET_ERROR_FORBIDDEN,
                            "path compression failed");
                return false;
            }
        }

        widget->from_request.query_string = env->external_uri->query;

        widget->from_request.method = env->method;
        widget->from_request.body = widget->parent->for_focused.body;
        widget->parent->for_focused.body = NULL;
    } else if (widget_descendant_has_focus(widget)) {
        /* we are the parent (or grant-parent) of the focused widget.
           store the relative focus_ref. */

        widget->from_request.focus_ref = widget->parent->from_request.focus_ref->next;
        widget->parent->from_request.focus_ref = NULL;

        widget->for_focused = widget->parent->for_focused;
        widget->parent->for_focused.body = NULL;
    }

    return true;
}

gcc_pure
static inline bool
widget_should_sync_session(const struct widget *widget)
{
    /* do not save to session when this is a POST request */
    if (widget->from_request.body != NULL)
        return false;

    /* save to session only if the effective view features the HTML
       processor */
    if (!widget_has_processor(widget))
        return false;

    return true;
}

void
widget_sync_session(struct widget *widget, struct session *session)
{
    assert(widget != NULL);
    assert(widget->parent != NULL);
    assert(widget->lazy.address == NULL);
    assert(widget->cls != NULL);
    assert(widget->cls->stateful);
    assert(widget->session_sync_pending);
    assert(!widget->session_save_pending);

    widget->session_sync_pending = false;

    if (!widget_should_sync_session(widget))
        /* not stateful in this request */
        return;

    /* are we focused? */

    if (widget_has_focus(widget)) {
        /* postpone until we have the widget's response; we do not
           know yet which view will be used until we have checked the
           response headers */

        widget->session_save_pending = true;
    } else {
        /* get query string from session */

        struct widget_session *ws = widget_get_session(widget, session, false);
        if (ws != NULL)
            session_to_widget(widget, ws);
    }
}

void
widget_save_session(struct widget *widget, struct session *session)
{
    assert(widget != NULL);
    assert(widget->parent != NULL);
    assert(widget->cls != NULL);
    assert(widget->cls->stateful);
    assert(!widget->session_sync_pending);
    assert(widget->session_save_pending);

    widget->session_save_pending = false;

    if (!widget_should_sync_session(widget))
        /* not stateful in this request */
        return;

    struct widget_session *ws = widget_get_session(widget, session, true);
    if (ws != NULL)
        widget_to_session(ws, widget);
}

void
widget_copy_from_location(struct widget *widget, struct session *session,
                          const char *location, size_t location_length,
                          struct pool *pool)
{
    const char *qmark;

    assert(widget != NULL);
    assert(widget->cls != NULL);

    widget->from_request.method = HTTP_METHOD_GET;
    widget->from_request.body = NULL;

    qmark = memchr(location, '?', location_length);
    if (qmark == NULL) {
        widget->from_request.path_info = p_strndup(pool, location,
                                                   location_length);
        strref_clear(&widget->from_request.query_string);
    } else {
        widget->from_request.path_info
            = p_strndup(pool, location, qmark - location);
        strref_set(&widget->from_request.query_string,
                   qmark + 1, location + location_length - (qmark + 1));
    }

    widget->lazy.address = NULL;

    if (session != NULL) {
        struct widget_session *ws;

        assert(widget->cls->stateful);

        ws = widget_get_session(widget, session, true);
        if (ws != NULL)
            widget_to_session(ws, widget);
    }
}
