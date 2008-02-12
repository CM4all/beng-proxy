/*
 * Concatenate several istreams.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream.h"

#include <assert.h>
#include <stdarg.h>

struct input {
    struct input *next;
    struct istream_cat *cat;
    istream_t istream;
};

struct istream_cat {
    struct istream output;
    unsigned reading;
    struct input *current;
    struct input inputs[1];
};


static inline int
cat_is_current(struct istream_cat *cat, struct input *input)
{
    return cat->current == input;
}

static void
cat_close(struct istream_cat *cat)
{
    struct input *input;

    while (cat->current != NULL) {
        input = cat->current;
        cat->current = input->next;
        if (input->istream != NULL)
            istream_free_unref_handler(&input->istream);
    }
    
    istream_invoke_abort(&cat->output);
}


/*
 * istream handler
 *
 */

static size_t
cat_input_data(const void *data, size_t length, void *ctx)
{
    struct input *input = ctx;
    struct istream_cat *cat = input->cat;

    assert(input->istream != NULL);

    if (!cat_is_current(cat, input))
        return 0;

    return istream_invoke_data(&cat->output, data, length);
}

static ssize_t
cat_input_direct(istream_direct_t type, int fd, size_t max_length, void *ctx)
{
    struct input *input = ctx;
    struct istream_cat *cat = input->cat;

    assert(input->istream != NULL);
    assert(cat_is_current(cat, input))

    return istream_invoke_direct(&cat->output, type, fd, max_length);
}

static void
cat_input_eof(void *ctx)
{
    struct input *input = ctx;
    struct istream_cat *cat = input->cat;

    assert(input->istream != NULL);

    istream_clear_unref(&input->istream);

    if (cat_is_current(cat, input)) {
        do {
            cat->current = cat->current->next;
        } while (cat->current != NULL && cat->current->istream == NULL);

        if (cat->current == NULL) {
            istream_invoke_eof(&cat->output);
        } else if (!cat->reading) {
            /* only call istream_read() if this function was not
               called from istream_cat_read() - in this case,
               istream_cat_read() would provide the loop.  This is
               advantageous because we avoid unnecessary recursing. */
            istream_read(cat->current->istream);
        }
    }
}

static void
cat_input_abort(void *ctx)
{
    struct input *input = ctx;
    struct istream_cat *cat = input->cat;

    istream_clear_unref(&input->istream);

    cat_close(cat);
}

static const struct istream_handler cat_input_handler = {
    .data = cat_input_data,
    .direct = cat_input_direct,
    .eof = cat_input_eof,
    .abort = cat_input_abort,
};


/*
 * istream implementation
 *
 */

static inline struct istream_cat *
istream_to_cat(istream_t istream)
{
    return (struct istream_cat *)(((char*)istream) - offsetof(struct istream_cat, output));
}

static off_t
istream_cat_available(istream_t istream, int partial)
{
    struct istream_cat *cat = istream_to_cat(istream);
    struct input *input;
    off_t available = 0, a;

    for (input = cat->current; input != NULL; input = input->next) {
        if (input->istream == NULL)
            continue;

        a = istream_available(input->istream, partial);
        if (a != (off_t)-1)
            available += a;
        else if (!partial)
            /* if the caller wants the exact number of bytes, and
               one input cannot provide it, we cannot provide it
               either */
            return (off_t)-1;
    }

    return available;
}

static void
istream_cat_read(istream_t istream)
{
    struct istream_cat *cat = istream_to_cat(istream);
    struct input *prev;

    pool_ref(cat->output.pool);

    cat->reading = 1;

    do {
        while (cat->current != NULL && cat->current->istream == NULL)
            cat->current = cat->current->next;

        if (cat->current == NULL) {
            istream_invoke_eof(&cat->output);
            break;
        }

        istream_handler_set_direct(cat->current->istream, cat->output.handler_direct);

        prev = cat->current;
        istream_read(cat->current->istream);
    } while (cat->current != NULL && cat->current != prev);

    cat->reading = 0;

    pool_unref(cat->output.pool);
}

static void
istream_cat_close(istream_t istream)
{
    struct istream_cat *cat = istream_to_cat(istream);

    cat_close(cat);
}

static const struct istream istream_cat = {
    .available = istream_cat_available,
    .read = istream_cat_read,
    .close = istream_cat_close,
};


/*
 * constructor
 *
 */

istream_t
istream_cat_new(pool_t pool, ...)
{
    struct istream_cat *cat;
    va_list ap;
    unsigned num = 0;
    istream_t istream;
    struct input **next_p, *input;

    va_start(ap, pool);
    while (va_arg(ap, istream_t) != NULL)
        ++num;
    va_end(ap);

    assert(num > 0);

    cat = p_malloc(pool, sizeof(*cat) + (num - 1) * sizeof(cat->inputs));
    cat->output = istream_cat;
    cat->output.pool = pool;
    cat->reading = 0;

    va_start(ap, pool);
    num = 0;
    next_p = &cat->current;
    while ((istream = va_arg(ap, istream_t)) != NULL) {
        assert(!istream_has_handler(istream));

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
