//
// Emulation of the Google Gadget JavaScript API.
//
// Author: Max Kellermann <mk@cm4all.com>
//

function _IG_Prefs(prefix) {
    this.getInt = function() {
        return 0;
    }

    this.getBool = function() {
        return true;
    }

    this.getString = function() {
        return "";
    }

    return this;
}

function _IG_Analytics(foo, bar) {
}

function urchinTracker(page) {
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

function _IG_AdjustIFrameHeight() {
}

function _gel(id) {
    return document.getElementById(id);
}

function _gelstn(tagname) {
    return document.getElementsByTagName(tagname);
}
