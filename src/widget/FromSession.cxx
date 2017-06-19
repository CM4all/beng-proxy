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

void
Widget::SaveToSession(WidgetSession &ws) const
try {
    assert(cls != nullptr);
    assert(cls->stateful); /* cannot save state for stateless widgets */

    auto &p = ws.session.parent.pool;

    ws.path_info.Set(p, from_request.path_info);

    if (from_request.query_string.IsEmpty())
        ws.query_string.Clear(p);
    else
        ws.query_string.Set(p, from_request.query_string);
} catch (std::bad_alloc) {
}

void
Widget::LoadFromSession(const WidgetSession &ws)
{
    assert(cls != nullptr);
    assert(cls->stateful); /* cannot load state from stateless widgets */
    assert(lazy.address == nullptr);

    from_request.path_info = ws.path_info;

    if (ws.query_string != nullptr)
        from_request.query_string = ws.query_string;
}

void
Widget::LoadFromSession(RealmSession &session)
{
    assert(parent != nullptr);
    assert(lazy.address == nullptr);
    assert(cls != nullptr);
    assert(cls->stateful);
    assert(session_sync_pending);
    assert(!session_save_pending);

    session_sync_pending = false;

    if (!ShouldSyncSession())
        /* not stateful in this request */
        return;

    /* are we focused? */

    if (HasFocus()) {
        /* postpone until we have the widget's response; we do not
           know yet which view will be used until we have checked the
           response headers */

        session_save_pending = true;
    } else {
        /* get query string from session */

        WidgetSession *ws = widget_get_session(this, &session, false);
        if (ws != nullptr)
            LoadFromSession(*ws);
    }
}

void
Widget::SaveToSession(RealmSession &session)
{
    assert(parent != nullptr);
    assert(cls != nullptr);
    assert(cls->stateful);
    assert(!session_sync_pending);
    assert(session_save_pending);

    session_save_pending = false;

    if (!ShouldSyncSession())
        /* not stateful in this request */
        return;

    WidgetSession *ws = widget_get_session(this, &session, true);
    if (ws != nullptr)
        SaveToSession(*ws);
}
