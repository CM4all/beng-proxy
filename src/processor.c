/*
 * Process CM4all commands in a HTML stream, e.g. embeddings.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "processor.h"
#include "strutil.h"

#include <sys/mman.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

struct substitution {
    struct substitution *next;
    off_t start, end;
};

enum parser_state {
    PARSER_NONE,
    PARSER_START,
    PARSER_NAME,
    PARSER_ELEMENT,
    PARSER_SHORT,
    PARSER_INSIDE,
};

struct processor {
    pool_t pool;
    const char *path;
    int fd;
    off_t source_length, content_length, position;
    char *map;

    enum parser_state parser_state;
    off_t element_offset;
    size_t match_length;
    char element_name[64];
    size_t element_name_length;

    struct substitution *first_substitution, **append_substitution_p;

    const struct processor_handler *handler;
    void *handler_ctx;
};

static const char element_start[] = "<c:";
static const char element_end[] = "</c:";

processor_t
processor_new(pool_t pool,
              const struct processor_handler *handler, void *ctx)
{
    processor_t processor = p_malloc(pool, sizeof(*processor));

    assert(handler != NULL);
    assert(handler->input != NULL);
    assert(handler->meta != NULL);
    assert(handler->output != NULL);
    assert(handler->output_finished != NULL);

    processor->pool = pool;
    processor->fd = -1;
    processor->source_length = 0;
    processor->content_length = 0;
    processor->map = NULL;

    processor->parser_state = PARSER_NONE;
    processor->first_substitution = NULL;
    processor->append_substitution_p = &processor->first_substitution;

    processor->handler = handler;
    processor->handler_ctx = ctx;

    /* XXX */
    processor->fd = open("/tmp/beng-processor.tmp", O_CREAT|O_EXCL|O_RDWR, 0777);
    if (processor->fd < 0) {
        perror("failed to create temporary file");
        return NULL;
    }
    unlink("/tmp/beng-processor.tmp");

    return processor;
}

static void
processor_close(processor_t processor)
{
    assert(processor != NULL);

    if (processor->fd >= 0) {
        close(processor->fd);
        processor->fd = -1;
    }

    if (processor->map != NULL) {
        munmap(processor->map, (size_t)processor->source_length);
        processor->map = NULL;
    }

    if (processor->handler != NULL) {
        const struct processor_handler *handler = processor->handler;
        void *handler_ctx = processor->handler_ctx;

        processor->handler = NULL;
        processor->handler_ctx = NULL;

        if (handler->free != NULL)
            handler->free(handler_ctx);
    }
}

void
processor_free(processor_t *processor_r)
{
    processor_t processor = *processor_r;
    *processor_r = NULL;
    assert(processor != NULL);

    processor_close(processor);
}

static void
processor_element_finished(processor_t processor, off_t end)
{
    struct substitution *s = p_malloc(processor->pool, sizeof(*s));

    s->next = NULL;
    s->start = processor->element_offset;
    s->end = end;

    /* subtract the command length from content_length */
    processor->content_length -= s->end - s->start;

    *processor->append_substitution_p = s;
    processor->append_substitution_p = &s->next;
}

static void
processor_parse_input(processor_t processor, const char *start, size_t length)
{
    const char *buffer = start, *end = start + length, *p;

    assert(processor != NULL);
    assert(buffer != NULL);
    assert(length > 0);

    while (buffer < end) {
        switch (processor->parser_state) {
        case PARSER_NONE:
            /* find first character */
            p = memchr(buffer, element_start[0], end - buffer);
            if (p == NULL)
                return;

            processor->parser_state = PARSER_START;
            processor->element_offset = processor->source_length + (off_t)(p - start);
            processor->match_length = 1;
            buffer = p + 1;
            break;

        case PARSER_START:
            /* compare more characters */
            assert(processor->match_length > 0);
            assert(processor->match_length < sizeof(element_start) - 1);

            do {
                if (*buffer != element_start[processor->match_length]) {
                    processor->parser_state = PARSER_NONE;
                    break;
                }

                ++processor->match_length;
                ++buffer;

                if (processor->match_length == sizeof(element_start) - 1) {
                    processor->parser_state = PARSER_NAME;
                    processor->element_name_length = 0;
                    break;
                }
            } while (unlikely(buffer < end));

            break;

        case PARSER_NAME:
            /* copy element name */
            while (buffer < end) {
                if (char_is_alphanumeric(*buffer)) {
                    if (processor->element_name_length == sizeof(processor->element_name)) {
                        /* name buffer overflowing */
                        processor->parser_state = PARSER_NONE;
                        break;
                    }

                    processor->element_name[processor->element_name_length++] = *buffer++;
                } else if ((char_is_whitespace(*buffer) || *buffer == '/' || *buffer == '>') &&
                           processor->element_name_length > 0) {
                    processor->parser_state = PARSER_ELEMENT;
                    break;
                } else {
                    processor->parser_state = PARSER_NONE;
                    break;
                }
            }

            break;

        case PARSER_ELEMENT:
            do {
                if (char_is_whitespace(*buffer)) {
                    ++buffer;
                } else if (*buffer == '/') {
                    processor->parser_state = PARSER_SHORT;
                    ++buffer;
                    break;
                } else if (*buffer == '>') {
                    processor->parser_state = PARSER_INSIDE;
                    ++buffer;
                    processor_element_finished(processor, processor->source_length + (off_t)(buffer - start));
                    break;
                } else {
                    processor->parser_state = PARSER_NONE;
                    break;
                }
            } while (buffer < end);

            break;

        case PARSER_SHORT:
            do {
                if (char_is_whitespace(*buffer)) {
                    ++buffer;
                } else if (*buffer == '>') {
                    processor->parser_state = PARSER_NONE;
                    ++buffer;
                    processor_element_finished(processor, processor->source_length + (off_t)(buffer - start));
                    break;
                } else {
                    processor->parser_state = PARSER_NONE;
                    break;
                }
            } while (buffer < end);

            break;

        case PARSER_INSIDE:
            /* XXX */
            processor->parser_state = PARSER_NONE;
            break;
        }
    }
}

size_t
processor_input(processor_t processor, const void *buffer, size_t length)
{
    ssize_t nbytes;

    assert(processor != NULL);
    assert(processor->fd >= 0);
    assert(buffer != NULL);
    assert(length > 0);

    nbytes = write(processor->fd, buffer, length);
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

    processor_parse_input(processor, (const char*)buffer, (size_t)nbytes);

    processor->source_length += (off_t)nbytes;
    processor->content_length += (off_t)nbytes;

    if (processor->source_length >= 8 * 1024 * 1024) {
        fprintf(stderr, "file too large for processor\n");
        processor_close(processor);
        return 0;
    }

    return (size_t)nbytes;
}

void
processor_input_finished(processor_t processor)
{
    off_t ret;

    assert(processor != NULL);
    assert(processor->fd >= 0);

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

    processor->handler->meta("text/html", processor->content_length,
                             processor->handler_ctx);
}

void
processor_output(processor_t processor)
{
    size_t rest, nbytes = 0;

    assert(processor != NULL);
    assert(processor->map != NULL);

    rest = (size_t)(processor->content_length - processor->position);
    if (rest > 0) {
        nbytes = processor->handler->output(processor->map + processor->position,
                                            rest, processor->handler_ctx);
        assert(nbytes <= rest);
        processor->position += nbytes;
    }

    if (nbytes == rest) {
        const struct processor_handler *handler = processor->handler;
        void *handler_ctx = processor->handler_ctx;

        munmap(processor->map, (size_t)processor->source_length);
        processor->map = NULL;

        handler->output_finished(handler_ctx);

        processor_close(processor);
    }
}
