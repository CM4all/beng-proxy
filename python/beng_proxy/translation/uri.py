#
# Build URIs.
#
# Author: Max Kellermann <mk@cm4all.com>
#

import urllib

def absolute_uri(request, scheme=None, host=None, uri=None, query_string=None,
                 param=None):
    """Returns the absolute URI of this request.  You may override
    some of the attributes."""

    if scheme is None: scheme = "http"
    if host is None:
        host = request.host
        if host is None: host = "localhost"
    if uri is None:
        uri = request.raw_uri
        if uri is None: uri = "/"
    x = scheme + "://" + host + uri
    if request.args is not None:
        x += ";" + request.args

    if param is not None:
        if request.args is not None:
            x += "&"
        else:
            x += ";"
        x += "translate=" + urllib.quote(param)

    if query_string is None:
        query_string = request.query_string
    if query_string is not None:
        x += "?" + query_string
    return x
