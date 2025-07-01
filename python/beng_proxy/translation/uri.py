#
# Build URIs.
#
# Author: Max Kellermann <max.kellermann@ionos.com>
#

from urllib.parse import quote

from typing import TYPE_CHECKING
if TYPE_CHECKING:
    from .request import Request

def absolute_uri(request: 'Request',
                 scheme: str|None=None,
                 host: str|None=None,
                 uri: str|None=None,
                 query_string: str|None=None,
                 param: str|None=None) -> str:
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
        x += "translate=" + quote(param)

    if query_string is None:
        query_string = request.query_string
    if query_string is not None:
        x += "?" + query_string
    return x
