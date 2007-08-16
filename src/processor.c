/*
 * Process CM4all commands in a HTML stream, e.g. embeddings.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "processor.h"

#include <sys/mman.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>

struct processor {
    pool_t pool;
    const char *path;
    int fd;
    off_t content_length, position;
    char *map;

    const struct processor_handler *handler;
    void *handler_ctx;
};

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
    processor->content_length = 0;
    processor->map = NULL;

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
        munmap(processor->map, (size_t)processor->content_length);
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

size_t
processor_input(processor_t processor, const void *buffer, size_t length)
{
    ssize_t nbytes;

    assert(processor != NULL);
    assert(processor->fd >= 0);

    nbytes = write(processor->fd, buffer, length);
    if (nbytes < 0) {
        perror("write to temporary file failed");
        processor_close(processor);
        return 0;
    }

    processor->content_length += (off_t)length;

    if (processor->content_length >= 8 * 1024 * 1024) {
        fprintf(stderr, "file too large for processor\n");
        processor_close(processor);
        return 0;
    }

    return (size_t)length;
}

void
processor_input_finished(processor_t processor)
{
    off_t ret;

    assert(processor != NULL);
    assert(processor->fd >= 0);

    processor->map = mmap(NULL, (size_t)processor->content_length,
                          PROT_READ, MAP_PRIVATE, processor->fd,
                          0);
    if (processor->map == NULL) {
        perror("mmap() failed");
        processor_close(processor);
        return;
    }

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

        processor_close(processor);
        handler->output_finished(handler_ctx);
    }
}
