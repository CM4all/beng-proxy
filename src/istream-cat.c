/*
 * This istream filter adds HTTP chunking.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream.h"

#include <assert.h>
#include <stdarg.h>

#define MAX_INPUTS 16

struct input {
    struct input *next;
    struct istream_cat *cat;
    istream_t istream;
};

struct istream_cat {
    struct istream output;
    struct input *current;
    int direct_mode, blocking;
    struct input inputs[MAX_INPUTS];
};


static void
cat_close(struct istream_cat *cat)
{
    struct input *input;

    while (cat->current != NULL) {
        input = cat->current;
        cat->current = input->next;
        if (input->istream != NULL)
            istream_free_unref(&input->istream);
    }
    
    istream_invoke_free(&cat->output);
}


static size_t
cat_input_data(const void *data, size_t length, void *ctx)
{
    struct input *input = ctx;
    struct istream_cat *cat = input->cat;
    ssize_t nbytes;

    assert(input->istream != NULL);

    if (input != cat->current)
        return 0;

    nbytes = istream_invoke_data(&cat->output, data, length);
    if (nbytes == (ssize_t)length)
        cat->blocking = 0;

    return nbytes;
}

static ssize_t
cat_input_direct(istream_direct_t type, int fd, size_t max_length, void *ctx)
{
    struct input *input = ctx;
    struct istream_cat *cat = input->cat;

    assert(input->istream != NULL);
    assert(input == cat->current);

    return istream_invoke_direct(&cat->output, type, fd, max_length);
}

static void
cat_input_eof(void *ctx)
{
    struct input *input = ctx;
    struct istream_cat *cat = input->cat;

    assert(input->istream != NULL);

    istream_clear_unref_handler(&input->istream);

    if (input == cat->current) {
        cat->current = input->next;
        if (cat->current == NULL) {
            pool_ref(cat->output.pool);
            istream_invoke_eof(&cat->output);
            cat_close(cat);
            pool_unref(cat->output.pool);
        } else {
            istream_read(cat->current->istream);
        }
    }
}

static void
cat_input_free(void *ctx)
{
    struct input *input = ctx;
    struct istream_cat *cat = input->cat;

    if (input->istream != NULL)
        cat_close(cat);
}

static const struct istream_handler cat_input_handler = {
    .data = cat_input_data,
    .direct = cat_input_direct,
    .eof = cat_input_eof,
    .free = cat_input_free,
};


static inline struct istream_cat *
istream_to_cat(istream_t istream)
{
    return (struct istream_cat *)(((char*)istream) - offsetof(struct istream_cat, output));
}

static void
istream_cat_read(istream_t istream)
{
    struct istream_cat *cat = istream_to_cat(istream);

    cat->direct_mode = 0;

    pool_ref(cat->output.pool);

    do {
        while (cat->current != NULL && cat->current->istream == NULL)
            cat->current = cat->current->next;

        if (cat->current == NULL) {
            istream_invoke_eof(&cat->output);
            cat_close(cat);
            break;
        }

        cat->blocking = 1;
        istream_handler_set_direct(cat->current->istream, cat->output.handler_direct);
        istream_read(cat->current->istream);
    } while (cat->current != NULL && !cat->blocking);

    pool_unref(cat->output.pool);
}

static void
istream_cat_close(istream_t istream)
{
    struct istream_cat *cat = istream_to_cat(istream);

    cat_close(cat);
}

static const struct istream istream_cat = {
    .read = istream_cat_read,
    .close = istream_cat_close,
};


istream_t
istream_cat_new(pool_t pool, ...)
{
    struct istream_cat *cat = p_malloc(pool, sizeof(*cat));
    va_list ap;
    unsigned num = 0;
    istream_t istream;
    struct input **next_p = &cat->current, *input;

    cat->output = istream_cat;
    cat->output.pool = pool;

    va_start(ap, pool);
    while ((istream = va_arg(ap, istream_t)) != NULL) {
        assert(!istream_has_handler(istream));
        assert(num < MAX_INPUTS);

        input = &cat->inputs[num++];
        input->next = NULL;
        input->cat = cat;

        istream_assign_ref_handler(&input->istream, istream,
                                   &cat_input_handler, input,
                                   0);

        *next_p = input;
        next_p = &input->next;
    }
    va_end(ap);

    return istream_struct_cast(&cat->output);
}
