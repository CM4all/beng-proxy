/*
 * Widget declarations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_WIDGET_REQUEST_H
#define BENG_PROXY_WIDGET_REQUEST_H

#include "glibfwd.hxx"

#include <stddef.h>

struct pool;
struct Widget;
struct processor_env;
struct Session;

/**
 * Copy parameters from the request to the widget.
 */
bool
widget_copy_from_request(Widget &widget, struct processor_env &env,
                         GError **error_r);

/**
 * Synchronize the widget with its session.
 */
void
widget_sync_session(Widget &widget, Session &session);

/**
 * Save the current request to the session.  Call this after you have
 * received the widget's response if the session_save_pending flag was
 * set by widget_sync_session().
 */
void
widget_save_session(Widget &widget, Session &session);

/**
 * Overwrite request data, copy values from a HTTP redirect location.
 */
void
widget_copy_from_location(Widget &widget, Session *session,
                          const char *location, size_t location_length,
                          struct pool &pool);

#endif
