/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_HTML_ESCAPE_HXX
#define BENG_PROXY_ISTREAM_HTML_ESCAPE_HXX

struct pool;
class Istream;

Istream *
istream_html_escape_new(struct pool &pool, Istream &input);

#endif
