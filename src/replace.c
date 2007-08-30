/*
 * Process CM4all commands in a HTML stream, e.g. embeddings.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "replace.h"

#include <sys/mman.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>

static void
replace_to_next_substitution(struct replace *replace, struct substitution *s)
{
    assert(replace->first_substitution == s);
    assert(replace->position == s->start);
    assert(s->istream == NULL);
    assert(s->start <= s->end);

    replace->position = s->end;
    replace->first_substitution = s->next;

    assert(replace->first_substitution == NULL ||
           replace->first_substitution->start >= replace->position);

    if (!replace->read_locked)
        replace_read(replace);
}

static size_t
replace_substitution_data(const void *data, size_t length, void *ctx)
{
    struct substitution *s = ctx;
    struct replace *replace = s->replace;

    if (replace->fd >= 0)
        return 0;

    assert(replace->position <= s->start);
    assert(replace->first_substitution != NULL);
    assert(replace->first_substitution->start <= s->start);

    if (replace->first_substitution != s ||
        replace->position < s->start)
        return 0;

    return istream_invoke_data(replace->output, data, length);
}

static void
replace_substitution_free(void *ctx)
{
    struct substitution *s = ctx;
    struct replace *replace = ctx;

    assert(s->istream != NULL);
    pool_unref(s->istream->pool);
    s->istream = NULL;

    if (replace->first_substitution != s ||
        replace->position < s->start)
        return;

    replace_to_next_substitution(replace, s);
}

static const struct istream_handler replace_substitution_handler = {
    .data = replace_substitution_data,
    .free = replace_substitution_free,
};


int
replace_init(struct replace *replace, pool_t pool, istream_t output)
{
    assert(replace != NULL);

    replace->pool = pool;
    replace->output = output;

    replace->fd = -1;
    replace->source_length = 0;
    replace->map = NULL;

    replace->first_substitution = NULL;
    replace->append_substitution_p = &replace->first_substitution;
    replace->read_locked = 0;

    /* XXX */
    replace->fd = open("/tmp/beng-replace.tmp", O_CREAT|O_EXCL|O_RDWR, 0777);
    if (replace->fd < 0) {
        perror("failed to create temporary file");
        return -1;
    }
    unlink("/tmp/beng-replace.tmp");

    return 0;
}

void
replace_destroy(struct replace *replace)
{
    assert(replace != NULL);

    while (replace->first_substitution != NULL) {
        struct substitution *s = replace->first_substitution;
        replace->first_substitution = s->next;

        if (s->istream != NULL) {
            istream_close(s->istream);
            assert(s->istream == NULL);
        }
    }

    if (replace->fd >= 0) {
        close(replace->fd);
        replace->fd = -1;
    }

    if (replace->map != NULL) {
        munmap(replace->map, (size_t)replace->source_length);
        replace->map = NULL;
    }

    if (replace->output != NULL)
        istream_free(&replace->output);
}

size_t
replace_feed(struct replace *replace, const void *data, size_t length)
{
    ssize_t nbytes;

    assert(replace != NULL);
    assert(replace->fd >= 0);
    assert(replace->map == NULL);
    assert(data != NULL);
    assert(length > 0);

    nbytes = write(replace->fd, data, length);
    if (nbytes < 0) {
        perror("write to temporary file failed");
        replace_destroy(replace);
        return 0;
    }

    if (nbytes == 0) {
        fprintf(stderr, "disk full\n");
        replace_destroy(replace);
        return 0;
    }

    replace->source_length += (off_t)nbytes;

    return (size_t)nbytes;
}

void
replace_eof(struct replace *replace)
{
    int ret;

    assert(replace != NULL);
    assert(replace->fd >= 0);
    assert(replace->map == NULL);

    replace->map = mmap(NULL, (size_t)replace->source_length,
                        PROT_READ, MAP_PRIVATE, replace->fd,
                        0);
    if (replace->map == NULL) {
        perror("mmap() failed");
        replace_destroy(replace);
        return;
    }

    madvise(replace->map, (size_t)replace->source_length,
            MADV_SEQUENTIAL);

    ret = close(replace->fd);
    replace->fd = -1;
    if (ret == (off_t)-1) {
        perror("close() failed");
        replace_destroy(replace);
        return;
    }

    replace->position = 0;

    replace_read(replace);
}

void
replace_add(struct replace *replace, off_t start, off_t end,
            istream_t istream)
{
    struct substitution *s;

    assert(replace != NULL);
    assert(replace->fd >= 0);
    assert(replace->map == NULL);
    assert(start >= 0);
    assert(start <= end);
    assert(end <= replace->source_length);

    s = p_malloc(replace->pool, sizeof(*s));
    s->next = NULL;
    s->replace = replace;
    s->start = start;
    s->end = end;
    s->istream = istream;

    if (istream != NULL) {
        pool_ref(istream->pool);

        istream->handler = &replace_substitution_handler;
        istream->handler_ctx = s;
    }

    *replace->append_substitution_p = s;
    replace->append_substitution_p = &s->next;
}

static void
replace_read_substitution(struct replace *replace)
{
    while (replace->first_substitution != NULL &&
           replace->position == replace->first_substitution->start) {
        struct substitution *s = replace->first_substitution;

        replace->read_locked = 1;

        if (s->istream == NULL)
            replace_to_next_substitution(replace, s);
        else
            istream_read(s->istream);

        replace->read_locked = 0;

        /* we assume the substitution object is blocking if it hasn't
           reached EOF with this one call */
        if (s == replace->first_substitution)
            return;
    }
}

void
replace_read(struct replace *replace)
{
    size_t rest, nbytes;

    assert(replace != NULL);
    assert(replace->map != NULL);
    assert(replace->position <= replace->source_length);

    if (replace->fd >= 0)
        return;

    pool_ref(replace->pool);
    replace_read_substitution(replace);
    if (pool_unref(replace->pool) == 0)
        return;

    if (replace->first_substitution == NULL)
        rest = (size_t)(replace->source_length - replace->position);
    else if (replace->position < replace->first_substitution->start)
        rest = (size_t)(replace->first_substitution->start - replace->position);
    else
        rest = 0;

    if (rest > 0) {
        nbytes = istream_invoke_data(replace->output,
                                     replace->map + replace->position,
                                     rest);
        assert(nbytes <= rest);
        replace->position += nbytes;
    }

    if (replace->first_substitution == NULL &&
        replace->position == replace->source_length) {

        munmap(replace->map, (size_t)replace->source_length);
        replace->map = NULL;

        pool_ref(replace->pool);

        istream_invoke_eof(replace->output);
        replace_destroy(replace);

        pool_unref(replace->pool);
    }
}
