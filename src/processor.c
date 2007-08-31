/*
 * Process CM4all commands in a HTML stream, e.g. embeddings.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "processor.h"
#include "parser.h"
#include "strutil.h"
#include "replace.h"
#include "embed.h"

#include <sys/mman.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

typedef struct processor *processor_t;

struct processor {
    struct istream output;
    istream_t input;

    struct replace replace;

    struct parser parser;
    enum {
        TAG_NONE,
        TAG_EMBED,
        TAG_A,
        TAG_IMG,
    } tag;
    char *href;
};

static inline processor_t
istream_to_processor(istream_t istream)
{
    return (processor_t)(((char*)istream) - offsetof(struct processor, output));
}

static void
processor_output_stream_read(istream_t istream)
{
    processor_t processor = istream_to_processor(istream);

    if (processor->replace.fd >= 0)
        istream_read(processor->input);
    else
        replace_read(&processor->replace);
}

static void
processor_close(processor_t processor);

static void
processor_output_stream_close(istream_t istream)
{
    processor_t processor = istream_to_processor(istream);

    processor_close(processor);
}

static const struct istream processor_output_stream = {
    .read = processor_output_stream_read,
    .direct = NULL,
    .close = processor_output_stream_close,
};

static size_t
processor_input_data(const void *data, size_t length, void *ctx)
{
    processor_t processor = ctx;
    size_t nbytes;

    assert(processor != NULL);
    assert(data != NULL);
    assert(length > 0);

    processor->parser.position = processor->replace.source_length;

    nbytes = replace_feed(&processor->replace, data, length);
    if (nbytes == 0)
        return 0;

    parser_feed(&processor->parser, (const char*)data, nbytes);

    if (processor->replace.source_length >= 8 * 1024 * 1024) {
        fprintf(stderr, "file too large for processor\n");
        processor_close(processor);
        return 0;
    }

    return (size_t)nbytes;
}

static void
processor_input_eof(void *ctx)
{
    processor_t processor = ctx;

    assert(processor != NULL);
    assert(processor->input != NULL);

    processor->input->handler = NULL;
    pool_unref(processor->input->pool);
    processor->input = NULL;

    replace_eof(&processor->replace);
}

static void
processor_input_free(void *ctx)
{
    processor_t processor = ctx;

    assert(processor->input != NULL);

    pool_unref(processor->input->pool);
    processor->input = NULL;

    processor_close(processor); /* XXX */
}

static const struct istream_handler processor_input_handler = {
    .data = processor_input_data,
    .eof = processor_input_eof,
    .free = processor_input_free,
};


istream_t
processor_new(pool_t pool, istream_t istream)
{
    processor_t processor;
    int ret;

    assert(istream != NULL);
    assert(istream->handler == NULL);

#ifdef NDEBUG
    pool_ref(pool);
#else
    pool = pool_new_linear(pool, "processor", 4096);
#endif

    processor = p_malloc(pool, sizeof(*processor));

    processor->output = processor_output_stream;
    processor->output.pool = pool;

    processor->input = istream;
    istream->handler = &processor_input_handler;
    istream->handler_ctx = processor;
    pool_ref(processor->input->pool);

    ret = replace_init(&processor->replace, pool, &processor->output);
    if (ret < 0) {
        istream_free(&processor->input);
        return NULL;
    }

    parser_init(&processor->parser);

    return &processor->output;
}

static void
processor_close(processor_t processor)
{
    assert(processor != NULL);

    processor->replace.output = NULL;
    replace_destroy(&processor->replace);

    if (processor->input != NULL) {
        pool_t pool = processor->input->pool;
        istream_free(&processor->input);
        pool_unref(pool);
    }

    istream_invoke_free(&processor->output);

    pool_unref(processor->output.pool);
}

static inline processor_t
parser_to_processor(struct parser *parser)
{
    return (processor_t)(((char*)parser) - offsetof(struct processor, parser));
}

void
parser_element_start(struct parser *parser)
{
    processor_t processor = parser_to_processor(parser);

    if (parser->element_name_length == 7 &&
        memcmp(parser->element_name, "c:embed", 7) == 0) {
        processor->tag = TAG_EMBED;
        processor->href = NULL;
    } else if (parser->element_name_length == 1 &&
               parser->element_name[0] == 'a') {
        processor->tag = TAG_A;
    } else if (parser->element_name_length == 3 &&
               memcmp(parser->element_name, "img", 3) == 0) {
        processor->tag = TAG_IMG;
    } else {
        processor->tag = TAG_NONE;
    }
}

void
parser_attr_finished(struct parser *parser)
{
    processor_t processor = parser_to_processor(parser);

    if (processor->tag == TAG_EMBED && parser->attr_name_length == 4 &&
        memcmp(parser->attr_name, "href", 4) == 0)
        processor->href = p_strndup(processor->output.pool, parser->attr_value,
                                    parser->attr_value_length);
    else if (processor->tag == TAG_IMG && parser->attr_name_length == 3 &&
             memcmp(parser->attr_name, "src", 3) == 0)
        replace_add(&processor->replace,
                    processor->parser.attr_value_start,
                    processor->parser.attr_value_end,
                    istream_string_new(processor->output.pool, "http://dory.intern.cm-ag/icons/unknown.gif"));
    else if (processor->tag == TAG_A && parser->attr_name_length == 4 &&
             memcmp(parser->attr_name, "href", 4) == 0)
        replace_add(&processor->replace,
                    processor->parser.attr_value_start,
                    processor->parser.attr_value_end,
                    istream_string_new(processor->output.pool, "http://localhost:8080/beng.html"));
}

void
parser_element_finished(struct parser *parser, off_t end)
{
    processor_t processor = parser_to_processor(parser);
    istream_t istream;

    if (processor->tag != TAG_EMBED || processor->href == NULL)
        return;

    istream = embed_new(processor->output.pool, processor->href);
    istream = istream_cat_new(processor->output.pool,
                              istream_string_new(processor->output.pool, "<div class='embed'>"),
                              istream,
                              istream_string_new(processor->output.pool, "</div>"),
                              NULL);

    replace_add(&processor->replace, processor->parser.element_offset,
                end, istream);
}
