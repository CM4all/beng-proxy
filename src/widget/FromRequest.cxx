/*
 * Copy parameters from a request to the widget object.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Widget.hxx"
#include "Error.hxx"
#include "Class.hxx"
#include "Ref.hxx"
#include "session.hxx"
#include "penv.hxx"
#include "uri/uri_parser.hxx"
#include "puri_relative.hxx"
#include "AllocatorPtr.hxx"
#include "shm/dpool.hxx"

#include <string.h>
#include <assert.h>

bool
Widget::HasFocus() const
{
    assert(parent != nullptr);

    return id != nullptr &&
        parent->from_request.focus_ref != nullptr &&
        strcmp(id, parent->from_request.focus_ref->id) == 0 &&
        parent->from_request.focus_ref->next == nullptr;
}

bool
Widget::DescendantHasFocus() const
{
    assert(parent != nullptr);

    return id != nullptr &&
        parent->from_request.focus_ref != nullptr &&
        strcmp(id, parent->from_request.focus_ref->id) == 0 &&
        parent->from_request.focus_ref->next != nullptr;
}

bool
Widget::CopyFromRequest(struct processor_env &env, GError **error_r)
{
    assert(parent != nullptr);
    assert(lazy.address == nullptr);
    assert(from_request.path_info == nullptr);
    assert(from_request.query_string.IsEmpty());
    assert(from_request.focus_ref == nullptr);
    assert(from_request.method == HTTP_METHOD_GET);
    assert(from_request.body == nullptr);

    if (id == nullptr)
        return true;

    /* are we focused? */

    if (HasFocus()) {
        /* we're in focus.  forward query string and request body. */
        from_request.path_info = env.path_info;
        if (from_request.path_info != nullptr) {
            from_request.path_info =
                uri_compress(*env.pool, from_request.path_info);
            if (from_request.path_info == nullptr) {
                g_set_error(error_r, widget_quark(), WIDGET_ERROR_FORBIDDEN,
                            "path compression failed");
                return false;
            }
        }

        from_request.query_string = env.external_uri->query;

        from_request.method = env.method;
        from_request.body = parent->for_focused.body;
        parent->for_focused.body = nullptr;
    } else if (DescendantHasFocus()) {
        /* we are the parent (or grant-parent) of the focused widget.
           store the relative focus_ref. */

        from_request.focus_ref = parent->from_request.focus_ref->next;
        parent->from_request.focus_ref = nullptr;

        for_focused = parent->for_focused;
        parent->for_focused.body = nullptr;
    }

    return true;
}

void
Widget::CopyFromRedirectLocation(StringView location, RealmSession *session)
{
    assert(cls != nullptr);

    from_request.method = HTTP_METHOD_GET;
    from_request.body = nullptr;

    const char *qmark = location.Find('?');
    if (qmark == nullptr) {
        from_request.path_info = p_strdup(pool, location);
        from_request.query_string = nullptr;
    } else {
        from_request.path_info = p_strdup(pool,
                                          StringView(location.data, qmark));
        from_request.query_string = { qmark + 1,
                                      size_t(location.end() - (qmark + 1)) };
    }

    lazy.address = nullptr;

    if (session != nullptr) {
        assert(cls->stateful);

        auto *ws = widget_get_session(this, session, true);
        if (ws != nullptr)
            SaveToSession(*ws);
    }
}
