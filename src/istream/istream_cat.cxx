/*
 * Concatenate several istreams.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_cat.hxx"
#include "istream_oo.hxx"
#include "util/Cast.hxx"

#include <assert.h>
#include <stdarg.h>

struct CatIstream;

struct CatInput {
    CatIstream *cat;
    struct istream *istream;

    /* handler */

    size_t OnData(const void *data, size_t length);
    ssize_t OnDirect(FdType type, int fd, size_t max_length);
    void OnEof();
    void OnError(GError *error);
};

struct CatIstream {
    struct istream output;
    bool reading = false;
    unsigned current = 0;
    const unsigned num;
    CatInput inputs[1];

    CatIstream(struct pool &p, unsigned _num, va_list ap);

    CatInput &GetCurrent() {
        return inputs[current];
    }

    const CatInput &GetCurrent() const {
        return inputs[current];
    }

    bool IsCurrent(const CatInput &input) const {
        return &GetCurrent() == &input;
    }

    CatInput &Shift() {
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

inline size_t
CatInput::OnData(const void *data, size_t length)
{
    assert(istream != nullptr);

    if (!cat->IsCurrent(*this))
        return 0;

    return istream_invoke_data(&cat->output, data, length);
}

inline ssize_t
CatInput::OnDirect(FdType type, int fd, size_t max_length)
{
    assert(istream != nullptr);
    assert(cat->IsCurrent(*this));

    return istream_invoke_direct(&cat->output, type, fd, max_length);
}

inline void
CatInput::OnEof()
{
    assert(istream != nullptr);
    istream = nullptr;

    if (cat->IsCurrent(*this)) {
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

inline void
CatInput::OnError(GError *error)
{
    assert(istream != nullptr);
    istream = nullptr;

    cat->CloseAllInputs();

    istream_deinit_abort(&cat->output, error);
}


/*
 * istream implementation
 *
 */

static inline CatIstream *
istream_to_cat(struct istream *istream)
{
    return &ContainerCast2(*istream, &CatIstream::output);
}

static off_t
istream_cat_available(struct istream *istream, bool partial)
{
    CatIstream *cat = istream_to_cat(istream);
    off_t available = 0;

    for (auto *input = &cat->GetCurrent(), *end = &cat->inputs[cat->num];
         input < end; ++input) {
        if (input->istream == nullptr)
            continue;

        const off_t a = istream_available(input->istream, partial);
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
    CatIstream *cat = istream_to_cat(istream);

    pool_ref(cat->output.pool);

    cat->reading = true;

    unsigned prev;
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
    CatIstream *cat = istream_to_cat(istream);

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
    CatIstream *cat = istream_to_cat(istream);

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

inline CatIstream::CatIstream(struct pool &p, unsigned _num, va_list ap)
    :num(_num)
{
    istream_init(&output, &istream_cat, &p);

    unsigned i = 0;
    struct istream *istream;
    while ((istream = va_arg(ap, struct istream *)) != nullptr) {
        assert(!istream_has_handler(istream));

        CatInput *input = &inputs[i++];
        input->cat = this;

        istream_assign_handler(&input->istream, istream,
                               &MakeIstreamHandler<CatInput>::handler, input,
                               0);
    }

    assert(i == num);
}

struct istream *
istream_cat_new(struct pool *pool, ...)
{
    unsigned num = 0;
    va_list ap;
    va_start(ap, pool);
    while (va_arg(ap, struct istream *) != nullptr)
        ++num;
    va_end(ap);

    assert(num > 0);

    CatIstream *cat;
    auto p = p_malloc(pool, sizeof(*cat) + (num - 1) * sizeof(cat->inputs));
    va_start(ap, pool);
    cat = new(p) CatIstream(*pool, num, ap);
    va_end(ap);
    return &cat->output;
}
