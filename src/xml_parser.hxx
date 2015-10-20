/*
 * Parse CM4all commands in HTML documents.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_XML_PARSER_HXX
#define BENG_PROXY_XML_PARSER_HXX

#include "util/StringView.hxx"
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
    StringView name;
    XmlParserTagType type;
};

struct XmlParserAttribute {
    off_t name_start, value_start, value_end, end;
    StringView name, value;
};

class XmlParserHandler {
public:
    /**
     * A tag has started, and we already know its name.
     *
     * @return true if attributes should be parsed, false otherwise
     * (saves CPU cycles; tag_finished() is not called)
     */
    virtual bool OnXmlTagStart(const XmlParserTag &tag) = 0;

    virtual void OnXmlTagFinished(const XmlParserTag &tag) = 0;
    virtual void OnXmlAttributeFinished(const XmlParserAttribute &attr) = 0;
    virtual size_t OnXmlCdata(const char *p, size_t length, bool escaped,
                              off_t start) = 0;
    virtual void OnXmlEof(off_t length) = 0;
    virtual void OnXmlError(GError *error) = 0;
};

class XmlParser;

XmlParser *
parser_new(struct pool &pool, struct istream *input,
           XmlParserHandler &handler);

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
