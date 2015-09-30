/*
 * Parse CM4all commands in HTML documents.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_XML_PARSER_HXX
#define BENG_PROXY_XML_PARSER_HXX

#include "strref.h"
#include "glibfwd.hxx"

#include <sys/types.h>

struct pool;
struct istream;

enum XmlParserTagType {
    TAG_OPEN,
    TAG_CLOSE,
    TAG_SHORT,

    /** XML processing instruction */
    TAG_PI,
};

struct XmlParserTag {
    off_t start, end;
    struct strref name;
    XmlParserTagType type;
};

struct XmlParserAttribute {
    off_t name_start, value_start, value_end, end;
    struct strref name, value;
};

struct XmlParserHandler {
    /**
     * A tag has started, and we already know its name.
     *
     * @return true if attributes should be parsed, false otherwise
     * (saves CPU cycles; tag_finished() is not called)
     */
    bool (*tag_start)(const XmlParserTag *tag, void *ctx);

    void (*tag_finished)(const XmlParserTag *tag, void *ctx);
    void (*attr_finished)(const XmlParserAttribute *attr, void *ctx);
    size_t (*cdata)(const char *p, size_t length, bool escaped, off_t start,
                    void *ctx);
    void (*eof)(void *ctx, off_t length);
    void (*abort)(GError *error, void *ctx);
};

class XmlParser;

XmlParser *
parser_new(struct pool &pool, struct istream *input,
           const XmlParserHandler *handler, void *handler_ctx);

/**
 * Close the parser object.  Note that this function does not
 * (indirectly) invoke the "abort" callback.
 */
void
parser_close(XmlParser *parser);

void
parser_read(XmlParser *parser);

void
parser_script(XmlParser *parser);

#endif
