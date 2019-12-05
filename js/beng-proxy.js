//
// The beng-proxy JavaScript library.
//
// Author: Max Kellermann <mk@cm4all.com>
//

function beng_widget_uri(base_uri, session_id, frame, focus, mode,
                         path, translate, view) {
    function _beng_proxy_escape(x)
    {
        return encodeURIComponent(x).replace(/%/g, '$');
    }

    if (base_uri == null ||
        (mode != null && mode != "focus" && mode != "frame" &&
         mode != "partial" && mode != "save"))
        return null;

    let uri = base_uri + ";session=" + _beng_proxy_escape(session_id || "");
    if (focus != null) {
        if (mode == "frame")
            mode = "partial";

        uri += "&focus=" + _beng_proxy_escape(focus);
        if (mode == "partial" || mode == "save")
            frame = focus;

        if (frame != null) {
            uri += "&frame=" + _beng_proxy_escape(frame);

            if (view != null)
                uri += "&view=" + _beng_proxy_escape(view);
        }

        if (path != null) {
            let query_string = null;
            let qmark = path.indexOf("?");
            if (qmark >= 0) {
                query_string = path.substring(qmark);
                path = path.substring(0, qmark);
            }
            uri += "&path=" + _beng_proxy_escape(path);
            if (query_string != null)
                uri += query_string;
        }
    }

    if (translate != null)
        uri += "&translate=" + _beng_proxy_escape(translate);

    return uri;
}

(window.cm4all && window.cm4all.widgets) || (function() {
    window.cm4all = window.cm4all || {};
    window.cm4all.widgets = window.cm4all.widgets || {};
    window.cm4all.widgets.register = window.cm4all.widgets.register || function(base, session, frame, path) {
        window.cm4all.widgets[path] = {
            url: function(pathInfo, options) {
                options = options || {};
                return beng_widget_uri(base, session, frame, path, options.mode || "partial", pathInfo || '');
            }
        };
    };
})();
