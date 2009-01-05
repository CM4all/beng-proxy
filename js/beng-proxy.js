//
// The beng-proxy JavaScript library.
//
// Author: Max Kellermann <mk@cm4all.com>
//

function beng_widget_uri(base_uri, session_id, frame, focus, mode,
                         path, translate) {
    if (base_uri == null ||
        (mode != null && mode != "focus" && mode != "frame" &&
         mode != "partial" && mode != "proxy" && mode != "save"))
        return null;

    var uri = base_uri + ";session=" + escape(session_id);
    if (focus != null) {
        if (mode == "frame")
            mode = "partial";

        uri += "&focus=" + escape(focus);
        if (mode == "partial" || mode == "proxy" || mode == "save")
            frame = focus;
        if (frame != null)
            uri += "&frame=" + escape(frame);
        if (mode == "proxy")
            uri += "&raw=1";
        if (mode == "save")
            uri += "&save=1";
        if (path != null) {
            var query_string = null;
            var qmark = path.indexOf("?");
            if (qmark >= 0) {
                query_string = path.substring(qmark);
                path = path.substring(0, qmark);
            }
            uri += "&path=" + escape(path);
            if (query_string != null)
                uri += query_string;
        }
    }

    if (parameter != null)
        uri += "&translate=" + escape(translate);

    return uri;
}
