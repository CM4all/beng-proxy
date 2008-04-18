//
// Emulation of the Google Gadget JavaScript API.
//
// Author: Max Kellermann <mk@cm4all.com>
//

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

function _IG_Prefs(dummy) {
    this.values = new Array();

    if (window._beng_proxy_widget_prefs != null) {
        values = window._beng_proxy_widget_prefs.split('&');
        for (var i = 0; i < values.length; ++i) {
            var key = values[i], value;
            i = key.indexOf('=');
            if (i >= 0) {
                value = key.substring(i + 1);
                key = key.substring(0, i);
            } else
                value = "";
            this.values[unescape(key)] = unescape(value);
        }
    }

    this.getInt = function(name) {
        return parseInt(this.getString(name));
    }

    this.getBool = function(name) {
        var value = this.getString(name);
        return value == "1" || value == "yes" || value == "true";
    }

    this.getString = function(name) {
        return this.values[name];
    }

    this.getArray = function(name) {
        var value = new Array();
        // XXX
        return value;
    }

    this.set = function(name, value) {
        this.values[name] = value;
        var url = window.location.href +
            "&focus=" + escape(_beng_proxy_widget_path) + "&save=1?" +
            escape(name) + "=" + escape(value);
        var req = beng_proxy_request();
        if (req == null)
            return null;
        req.open("GET", url, true);
        req.send(null);
    }

    return this;
}

function _IG_Analytics(foo, bar) {
}

function urchinTracker(page) {
}

function _IG_FetchContent(url, callback) {
    var req = beng_proxy_request();
    if (req == null)
        return null;
    req.onreadystatechange = function() {
        if (req.readyState == 4 && req.status >= 200 && req.status < 300) {
            callback(req.responseText);
        } else {
            callback(null);
        }
    }
    req.open("GET", url, true);
    req.send(null);
    return req;
}

function _IG_FetchXmlContent(url, callback) {
    var req = beng_proxy_request();
    if (req == null)
        return null;
    req.onreadystatechange = function() {
        if (req.readyState == 4 && req.status >= 200 && req.status < 300) {
            callback(req.responseXML);
        } else {
            callback(null);
        }
    }
    req.open("GET", url, true);
    req.send(null);
    return req;
}

function _IG_SetTitle(title) {
}

function _IG_GetCachedUrl(url) {
    return url;
}

function _IG_GetImageUrl(url) {
    return url;
}

function _IG_AdjustIFrameHeight() {
}

function _IG_RegisterOnloadHandler(load) {
    load();
}

function _IG_AddDOMEventHandler(window, event, handler) {
}

function _gel(id) {
    return document.getElementById(id);
}

function _gelstn(tagname) {
    return document.getElementsByTagName(tagname);
}
