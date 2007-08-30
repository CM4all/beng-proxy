/*
 * Process CM4all commands in a HTML stream, e.g. embeddings.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "processor.h"
#include "parser.h"
#include "strutil.h"
#include "substitution.h"

#include <sys/mman.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

typedef struct processor *processor_t;

struct processor {
    const char *path;
    int fd;
    off_t source_length, position;
    char *map;

    struct parser parser;
    enum {
        TAG_NONE,
        TAG_EMBED,
    } tag;
    char *href;

    struct substitution *first_substitution, **append_substitution_p;

    int output_locked;

    struct istream output;
    istream_t input;
};

static inline processor_t
istream_to_processor(istream_t istream)
{
    return (processor_t)(((char*)istream) - offsetof(struct processor, output));
}

static void
processor_output(processor_t processor);

static void
processor_output_stream_read(istream_t istream)
{
    processor_t processor = istream_to_processor(istream);

    if (processor->fd >= 0)
        istream_read(processor->input);
    else
        processor_output(processor);
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

    ssize_t nbytes;

    assert(processor != NULL);
    assert(processor->fd >= 0);
    assert(data != NULL);
    assert(length > 0);

    nbytes = write(processor->fd, data, length);
    if (nbytes < 0) {
        perror("write to temporary file failed");
        processor_close(processor);
        return 0;
    }

    if (nbytes == 0) {
        fprintf(stderr, "disk full\n");
        processor_close(processor);
        return 0;
    }

    processor->parser.position = processor->source_length;
    parser_feed(&processor->parser, (const char*)data, (size_t)nbytes);

    processor->source_length += (off_t)nbytes;

    if (processor->source_length >= 8 * 1024 * 1024) {
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
    off_t ret;

    assert(processor != NULL);
    assert(processor->fd >= 0);
    assert(processor->input != NULL);

    processor->input->handler = NULL;
    pool_unref(processor->input->pool);
    processor->input = NULL;

    processor->map = mmap(NULL, (size_t)processor->source_length,
                          PROT_READ, MAP_PRIVATE, processor->fd,
                          0);
    if (processor->map == NULL) {
        perror("mmap() failed");
        processor_close(processor);
        return;
    }

    madvise(processor->map, (size_t)processor->source_length,
            MADV_SEQUENTIAL);

    ret = close(processor->fd);
    processor->fd = -1;
    if (ret == (off_t)-1) {
        perror("close() failed");
        processor_close(processor);
        return;
    }

    processor->position = 0;

    /* XXX processor->handler->meta("text/html", processor->handler_ctx); */
    processor_output(processor);
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

    assert(istream != NULL);
    assert(istream->handler == NULL);

#ifdef NDEBUG
    pool_ref(pool);
#else
    pool = pool_new_linear(pool, "processor", 4096);
#endif

    processor = p_malloc(pool, sizeof(*processor));

    processor->fd = -1;
    processor->source_length = 0;
    processor->map = NULL;

    parser_init(&processor->parser);

    processor->first_substitution = NULL;
    processor->append_substitution_p = &processor->first_substitution;
    processor->output_locked = 0;

    processor->output = processor_output_stream;
    processor->output.pool = pool;
    processor->input = istream;
    istream->handler = &processor_input_handler;
    istream->handler_ctx = processor;
    pool_ref(processor->input->pool);

    /* XXX */
    processor->fd = open("/tmp/beng-processor.tmp", O_CREAT|O_EXCL|O_RDWR, 0777);
    if (processor->fd < 0) {
        perror("failed to create temporary file");
        return NULL;
    }
    unlink("/tmp/beng-processor.tmp");

    return &processor->output;
}

static void
processor_close(processor_t processor)
{
    assert(processor != NULL);

    while (processor->first_substitution != NULL) {
        struct substitution *s = processor->first_substitution;
        processor->first_substitution = s->next;
        substitution_close(s);
    }

    if (processor->fd >= 0) {
        close(processor->fd);
        processor->fd = -1;
    }

    if (processor->map != NULL) {
        munmap(processor->map, (size_t)processor->source_length);
        processor->map = NULL;
    }

    if (processor->input != NULL) {
        istream_free(&processor->input);
        pool_unref(processor->input->pool);
    }

    istream_invoke_free(&processor->output);

    pool_unref(processor->output.pool);
}

static size_t
processor_substitution_output(struct substitution *s,
                              const void *data, size_t length)
{
    processor_t processor = s->handler_ctx;

    if (processor->fd >= 0)
        return 0;

    assert(processor->position <= s->start);

    if (processor->first_substitution != s ||
        processor->position < processor->first_substitution->start)
        return 0;

    return istream_invoke_data(&processor->output, data, length);
}

static void
processor_substitution_eof(struct substitution *s)
{
    processor_t processor = s->handler_ctx;

    assert(processor->fd < 0);
    assert(processor->first_substitution == s);
    assert(processor->position == processor->first_substitution->start);

    processor->position = s->end;
    processor->first_substitution = s->next;
    substitution_close(s);

    if (!processor->output_locked)
        processor_output(processor);
}

static const struct substitution_handler processor_substitution_handler = {
    .output = processor_substitution_output,
    .eof = processor_substitution_eof,
};

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
    } else {
        processor->tag = TAG_NONE;
    }
}

void
parser_attr_finished(struct parser *parser, off_t end)
{
    processor_t processor = parser_to_processor(parser);

    (void)end;

    if (processor->tag == TAG_EMBED && parser->attr_name_length == 4 &&
        memcmp(parser->attr_name, "href", 4) == 0)
        processor->href = p_strndup(processor->output.pool, parser->attr_value,
                                    parser->attr_value_length);
}

void
parser_element_finished(struct parser *parser, off_t end)
{
    processor_t processor = parser_to_processor(parser);
    pool_t pool;
    struct substitution *s;

    if (processor->tag != TAG_EMBED || processor->href == NULL)
        return;

    pool = pool_new_linear(processor->output.pool, "processor_substitution", 16384);
    s = p_malloc(pool, sizeof(*s));
    s->next = NULL;
    s->start = processor->parser.element_offset;
    s->end = end;

    s->pool = pool;

    s->handler = &processor_substitution_handler;
    s->handler_ctx = processor;

    *processor->append_substitution_p = s;
    processor->append_substitution_p = &s->next;

    substitution_start(s, processor->href);
}

static void
processor_output_substitution(processor_t processor)
{
    while (processor->first_substitution != NULL &&
           processor->position == processor->first_substitution->start) {
        struct substitution *s = processor->first_substitution;

        processor->output_locked = 1;
        substitution_output(s);
        processor->output_locked = 0;

        /* we assume the substitution object is blocking if it hasn't
           reached EOF with this one call */
        if (s == processor->first_substitution)
            return;
    }
}

static void
processor_output(processor_t processor)
{
    size_t rest, nbytes;

    assert(processor != NULL);
    assert(processor->map != NULL);
    assert(processor->position <= processor->source_length);

    if (processor->fd >= 0)
        return;

    pool_ref(processor->output.pool);
    processor_output_substitution(processor);
    if (pool_unref(processor->output.pool) == 0)
        return;

    if (processor->first_substitution == NULL)
        rest = (size_t)(processor->source_length - processor->position);
    else if (processor->position < processor->first_substitution->start)
        rest = (size_t)(processor->first_substitution->start - processor->position);
    else
        rest = 0;

    if (rest > 0) {
        nbytes = istream_invoke_data(&processor->output,
                                     processor->map + processor->position,
                                     rest);
        assert(nbytes <= rest);
        processor->position += nbytes;
    }

    if (processor->first_substitution == NULL &&
        processor->position == processor->source_length) {
        pool_t pool = processor->output.pool;

        munmap(processor->map, (size_t)processor->source_length);
        processor->map = NULL;

        pool_ref(pool);

        istream_invoke_eof(&processor->output);

        processor_close(processor);

        pool_unref(pool);
    }
}
