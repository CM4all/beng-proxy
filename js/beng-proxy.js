function beng_proxy_request() {
    var req = null;
    if (window.XMLHttpRequest) {
    	try {
            return new XMLHttpRequest();
        } catch(e) {
            return null;
        }
    } else if(window.ActiveXObject) {
       	try {
            return new ActiveXObject("Msxml2.XMLHTTP");
      	} catch(e) {
            try {
                return new ActiveXObject("Microsoft.XMLHTTP");
            } catch(e) {
                return null;
            }
        }
    } else {
        return null;
    }
}

function beng_proxy_make_uri(focus, path, proxy) {
    var uri = this.uri + ";session=" + escape(this.session);
    if (focus != null) {
        uri += "&focus=" + escape(focus);
        if (proxy)
            uri += "&frame=" + escape(focus);
        if (path != null)
            uri += "&path=" + escape(path);
    }
    return uri;
}

function beng_proxy(session) {
    this.uri = String(window.location).replace(/[;?#].*/, "");
    this.session = session;

    this.make_uri = beng_proxy_make_uri;

    return this;
}

function beng_widget_get_widget(id) {
    var slash = id.indexOf("/");
    if (slash == -1)
        return this.widgets[id];
    var widget = this.widgets[id.substring(0, slash)];
    if (widget == null)
        return null;
    return widget.getWidget(id.substring(slash + 1));
}

function beng_root_widget(proxy) {
    this.parent = null;
    this.proxy = proxy;
    this.id = null;
    this.path = null;
    this.widgets = Array();

    this.getWidget = beng_widget_get_widget;

    return this;
}

function beng_widget_translate_uri(uri, proxy) {
    if (this.path == null)
        return null;

    return this.proxy.make_uri(this.path, uri, proxy);
}

function beng_widget_get(uri, onreadystatechange) {
    var url = this.translateURI(uri, true);
    if (url == null)
        return null;
    var req = beng_proxy_request();
    if (req == null)
        return null;
    req.onreadystatechange = onreadystatechange;
    req.open("GET", url, onreadystatechange != null);
    req.send(null);
    return req;
}

function beng_widget_reload(uri) {
    var widget = this;
    var req = this.get(uri, function() {
            if (req == null)
                return;
            if (req.readyState == 4 && req.status >= 200 && req.status < 300) {
                var element = widget.getElement();
                if (element != null)
                    element.innerHTML = req.responseText;
            }
        });
    return req;
}

function beng_widget(parent, id) {
    if (parent.parent == null)
        this.path = id;
    else if (parent.path == null)
        this.path = null;
    else
        this.path = parent.path + "/" + id;

    if (id != null)
        parent.widgets[id] = this;
    this.parent = parent;
    this.proxy = parent.proxy;
    this.id = id;
    this.widgets = Array();

    this.getWidget = beng_widget_get_widget;
    this.translateURI = beng_widget_translate_uri;
    this.get = beng_widget_get;
    this.reload = beng_widget_reload;
    this.getElement = function() {
        if (this.path == null)
            return null;
        var id = "beng_widget___" + this.path.replace(/\//g, "__") + "__";
        return document.getElementById(id);
    };

    return this;
}
