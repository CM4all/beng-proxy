#include "xml_parser.hxx"
#include "istream/istream.hxx"
#include "istream/istream_file.hxx"
#include "PInstance.hxx"
#include "fb_pool.hxx"
#include "util/Exception.hxx"
#include "util/PrintException.hxx"

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

static bool should_exit;

class MyXmlParserHandler final : public XmlParserHandler {
public:
    /* virtual methods from class XmlParserHandler */
    bool OnXmlTagStart(gcc_unused const XmlParserTag &tag) override {
        return false;
    }

    void OnXmlTagFinished(gcc_unused const XmlParserTag &tag) override {}
    void OnXmlAttributeFinished(gcc_unused const XmlParserAttribute &attr) override {}

    size_t OnXmlCdata(const char *p, size_t length, gcc_unused bool escaped,
                      gcc_unused off_t start) override {
        (void)write(1, p, length);
        return length;
    }

    void OnXmlEof(gcc_unused off_t length) override {
        should_exit = true;
    }

    void OnXmlError(std::exception_ptr ep) override {
        fprintf(stderr, "ABORT: %s\n", GetFullMessage(ep).c_str());
        exit(2);
    }
};

int
main(int argc, char **argv)
try {
    struct pool *pool;
    Istream *istream;

    (void)argc;
    (void)argv;

    const ScopeFbPoolInit fb_pool_init;
    PInstance instance;

    pool = pool_new_linear(instance.root_pool, "test", 8192);

    istream = istream_file_new(instance.event_loop, *pool,
                               "/dev/stdin", (off_t)-1);

    MyXmlParserHandler handler;
    auto *parser = parser_new(*pool, *istream, handler);

    while (!should_exit)
        parser_read(parser);

    pool_unref(pool);
} catch (...) {
    PrintException(std::current_exception());
    return EXIT_FAILURE;
}
