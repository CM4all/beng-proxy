/*
 * Copy parameters from a request to the widget object.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "widget.h"
#include "session.h"
#include "processor.h"
#include "uri-parser.h"
#include "strref-dpool.h"

#include <string.h>
#include <assert.h>

/** copy data from the widget to its associated session */
static void
widget_to_session(struct widget_session *ws, const struct widget *widget)
{
    assert(widget != NULL);
    assert(widget->class != NULL);
    assert(widget->class->stateful); /* cannot save state for stateless widgets */

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
    assert(widget->class != NULL);
    assert(widget->class->stateful); /* cannot load state from stateless widgets */
    assert(widget->lazy.address == NULL);

    widget->from_request.path_info = ws->path_info;

    if (ws->query_string != NULL)
        strref_set_c(&widget->from_request.query_string, ws->query_string);
}

void
widget_copy_from_request(struct widget *widget, struct processor_env *env)
{
    assert(widget != NULL);
    assert(widget->lazy.address == NULL);
    assert(widget->from_request.path_info == NULL);
    assert(strref_is_empty(&widget->from_request.query_string));
    assert(widget->from_request.proxy_ref == NULL);
    assert(widget->from_request.focus_ref == NULL);
    assert(widget->from_request.method == HTTP_METHOD_GET);
    assert(widget->from_request.body == NULL);
    assert(!widget->from_request.proxy);

    if (widget->id == NULL || widget->parent == NULL)
        return;

    /* is this widget being proxied? */

    if (widget->parent->from_request.proxy_ref != NULL &&
        strcmp(widget->id, widget->parent->from_request.proxy_ref->id) == 0) {
        widget->from_request.proxy_ref = widget->parent->from_request.proxy_ref->next;

        if (widget->from_request.proxy_ref == NULL) {
            widget->from_request.proxy = true;
            if (strmap_get(env->args, "raw") != NULL)
                widget->from_request.raw = true;
        } else
            widget->parent->from_request.proxy_ref = NULL;
    }

    /* are we focused? */

    if (widget->parent->from_request.focus_ref != NULL &&
        strcmp(widget->id, widget->parent->from_request.focus_ref->id) == 0 &&
        widget->parent->from_request.focus_ref->next == NULL) {
        /* we're in focus.  forward query string and request body. */
        widget->from_request.path_info = strmap_remove(env->args, "path");
        widget->from_request.query_string = env->external_uri->query;

        if (env->request_body != NULL) {
            /* XXX which method? */
            widget->from_request.method = HTTP_METHOD_POST;
            widget->from_request.body = env->request_body;
            env->request_body = NULL;
        }
    } else if (widget->parent->from_request.focus_ref != NULL &&
               strcmp(widget->id, widget->parent->from_request.focus_ref->id) == 0 &&
               widget->parent->from_request.focus_ref->next != NULL) {
        /* we are the parent (or grant-parent) of the focused widget.
           store the relative focus_ref. */

        widget->from_request.focus_ref = widget->parent->from_request.focus_ref->next;
        widget->parent->from_request.focus_ref = NULL;
    }
}

void
widget_sync_session(struct widget *widget, struct session *session)
{
    assert(widget != NULL);
    assert(widget->lazy.address == NULL);
    assert(widget->class != NULL);
    assert(widget->class->stateful);

    lock_lock(&session->lock);

    /* are we focused? */

    if (widget->id != NULL && widget->parent != NULL &&
        widget->parent->from_request.focus_ref != NULL &&
        strcmp(widget->id, widget->parent->from_request.focus_ref->id) == 0 &&
        widget->parent->from_request.focus_ref->next == NULL) {

        /* do not save to session when this is a raw or POST request */
        if (!widget->from_request.raw && widget->from_request.body == NULL) {
            struct widget_session *ws = widget_get_session(widget, session, true);
            if (ws != NULL)
                widget_to_session(ws, widget);
        }
    } else {
        /* get query string from session */

        struct widget_session *ws = widget_get_session(widget, session, false);
        if (ws != NULL)
            session_to_widget(widget, ws);
    }

    lock_unlock(&session->lock);

    if (widget->from_request.path_info == NULL)
        widget->from_request.path_info = widget->path_info;

    assert(widget->from_request.path_info != NULL);
}

void
widget_copy_from_location(struct widget *widget, struct session *session,
                          const char *location, size_t location_length,
                          pool_t pool)
{
    const char *qmark;

    assert(widget != NULL);
    assert(widget->class != NULL);
    assert(session != NULL);

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

    if (widget->class->stateful) {
        struct widget_session *ws;

        lock_lock(&session->lock);

        ws = widget_get_session(widget, session, true);
        if (ws != NULL)
            widget_to_session(ws, widget);

        lock_unlock(&session->lock);
    }
}
