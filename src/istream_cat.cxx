/*
 * Concatenate several istreams.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_cat.hxx"
#include "istream-internal.h"
#include "util/Cast.hxx"

#include <assert.h>
#include <stdarg.h>

struct input {
    struct istream_cat *cat;
    struct istream *istream;
};

struct istream_cat {
    struct istream output;
    bool reading;
    unsigned current, num;
    struct input inputs[1];

    struct input &GetCurrent() {
        return inputs[current];
    }

    const struct input &GetCurrent() const {
        return inputs[current];
    }

    bool IsCurrent(const struct input &input) const {
        return &GetCurrent() == &input;
    }

    struct input &Shift() {
        return inputs[current++];
    }

    bool IsEOF() const {
        return current == num;
    }

    void CloseAllInputs() {
        while (!IsEOF()) {
            auto &input = Shift();
            if (input.istream != nullptr)
                istream_close_handler(input.istream);
        }
    }
};


/*
 * istream handler
 *
 */

static size_t
cat_input_data(const void *data, size_t length, void *ctx)
{
    auto *input = (struct input *)ctx;
    struct istream_cat *cat = input->cat;

    assert(input->istream != nullptr);

    if (!cat->IsCurrent(*input))
        return 0;

    return istream_invoke_data(&cat->output, data, length);
}

static ssize_t
cat_input_direct(enum istream_direct type, int fd, size_t max_length,
                 void *ctx)
{
    auto *input = (struct input *)ctx;
    struct istream_cat *cat = input->cat;

    assert(input->istream != nullptr);
    assert(cat->IsCurrent(*input));

    return istream_invoke_direct(&cat->output, type, fd, max_length);
}

static void
cat_input_eof(void *ctx)
{
    auto *input = (struct input *)ctx;
    struct istream_cat *cat = input->cat;

    assert(input->istream != nullptr);
    input->istream = nullptr;

    if (cat->IsCurrent(*input)) {
        do {
            cat->Shift();
        } while (!cat->IsEOF() && cat->GetCurrent().istream == nullptr);

        if (cat->IsEOF()) {
            istream_deinit_eof(&cat->output);
        } else if (!cat->reading) {
            /* only call istream_read() if this function was not
               called from istream_cat_read() - in this case,
               istream_cat_read() would provide the loop.  This is
               advantageous because we avoid unnecessary recursing. */
            istream_read(cat->GetCurrent().istream);
        }
    }
}

static void
cat_input_abort(GError *error, void *ctx)
{
    auto *input = (struct input *)ctx;
    struct istream_cat *cat = input->cat;

    assert(input->istream != nullptr);
    input->istream = nullptr;

    cat->CloseAllInputs();

    istream_deinit_abort(&cat->output, error);
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
istream_to_cat(struct istream *istream)
{
    return &ContainerCast2(*istream, &istream_cat::output);
}

static off_t
istream_cat_available(struct istream *istream, bool partial)
{
    struct istream_cat *cat = istream_to_cat(istream);
    struct input *input, *end;
    off_t available = 0, a;

    for (input = &cat->GetCurrent(), end = &cat->inputs[cat->num];
         input < end; ++input) {
        if (input->istream == nullptr)
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
istream_cat_read(struct istream *istream)
{
    struct istream_cat *cat = istream_to_cat(istream);
    unsigned prev;

    pool_ref(cat->output.pool);

    cat->reading = true;

    do {
        while (!cat->IsEOF() && cat->GetCurrent().istream == nullptr)
            ++cat->current;

        if (cat->IsEOF()) {
            istream_deinit_eof(&cat->output);
            break;
        }

        istream_handler_set_direct(cat->GetCurrent().istream,
                                   cat->output.handler_direct);

        prev = cat->current;
        istream_read(cat->GetCurrent().istream);
    } while (!cat->IsEOF() && cat->current != prev);

    cat->reading = false;

    pool_unref(cat->output.pool);
}

static int
istream_cat_as_fd(struct istream *istream)
{
    struct istream_cat *cat = istream_to_cat(istream);

    /* we can safely forward the as_fd() call to our input if it's the
       last one */

    if (cat->current != cat->num - 1)
        /* not on last input */
        return -1;

    auto &i = cat->GetCurrent();
    int fd = istream_as_fd(i.istream);
    if (fd >= 0)
        istream_deinit(&cat->output);

    return fd;
}

static void
istream_cat_close(struct istream *istream)
{
    struct istream_cat *cat = istream_to_cat(istream);

    cat->CloseAllInputs();
    istream_deinit(&cat->output);
}

static const struct istream_class istream_cat = {
    .available = istream_cat_available,
    .read = istream_cat_read,
    .as_fd = istream_cat_as_fd,
    .close = istream_cat_close,
};


/*
 * constructor
 *
 */

struct istream *
istream_cat_new(struct pool *pool, ...)
{
    struct istream_cat *cat;
    va_list ap;
    unsigned num = 0;
    struct istream *istream;
    struct input *input;

    va_start(ap, pool);
    while (va_arg(ap, struct istream *) != nullptr)
        ++num;
    va_end(ap);

    assert(num > 0);

    cat = (struct istream_cat*)istream_new(pool, &istream_cat,
                                           sizeof(*cat) + (num - 1) * sizeof(cat->inputs));
    cat->reading = false;
    cat->current = 0;
    cat->num = num;

    va_start(ap, pool);
    num = 0;
    while ((istream = va_arg(ap, struct istream *)) != nullptr) {
        assert(!istream_has_handler(istream));

        input = &cat->inputs[num++];
        input->cat = cat;

        istream_assign_handler(&input->istream, istream,
                               &cat_input_handler, input,
                               0);
    }
    va_end(ap);

    return istream_struct_cast(&cat->output);
}
