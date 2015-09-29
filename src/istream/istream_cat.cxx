/*
 * Concatenate several istreams.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_cat.hxx"
#include "istream_oo.hxx"
#include "util/Cast.hxx"

#include <boost/intrusive/slist.hpp>

#include <iterator>

#include <assert.h>
#include <stdarg.h>

struct CatIstream;

struct CatInput
    : boost::intrusive::slist_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>> {

    CatIstream *cat;
    struct istream *istream;

    /* handler */

    size_t OnData(const void *data, size_t length);
    ssize_t OnDirect(FdType type, int fd, size_t max_length);
    void OnEof();
    void OnError(GError *error);

    struct Disposer {
        void operator()(CatInput *input) {
            if (input->istream != nullptr)
                istream_close_handler(input->istream);
        }
    };
};

struct CatIstream {
    struct istream output;
    bool reading = false;

    typedef boost::intrusive::slist<CatInput,
                                    boost::intrusive::constant_time_size<false>> InputList;
    InputList inputs;

    CatIstream(struct pool &p, va_list ap);

    CatInput &GetCurrent() {
        return inputs.front();
    }

    const CatInput &GetCurrent() const {
        return inputs.front();
    }

    bool IsCurrent(const CatInput &input) const {
        return &GetCurrent() == &input;
    }

    bool IsEOF() const {
        return inputs.empty();
    }

    /**
     * Remove all nulled leading inputs.
     *
     * @return false if there are no more inputs
     */
    bool AutoShift() {
        while (true) {
            if (IsEOF())
                return false;

            if (GetCurrent().istream != nullptr)
                return true;

            inputs.pop_front();
        }
    }

    void CloseAllInputs() {
        inputs.clear_and_dispose(CatInput::Disposer());
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
        if (!cat->AutoShift()) {
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

    for (const auto &input : cat->inputs) {
        if (input.istream == nullptr)
            continue;

        const off_t a = istream_available(input.istream, partial);
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
    const ScopePoolRef ref(*cat->output.pool TRACE_ARGS);

    cat->reading = true;

    CatIstream::InputList::const_iterator prev;
    do {
        if (!cat->AutoShift()) {
            istream_deinit_eof(&cat->output);
            break;
        }

        istream_handler_set_direct(cat->GetCurrent().istream,
                                   cat->output.handler_direct);

        prev = cat->inputs.begin();
        istream_read(cat->GetCurrent().istream);
    } while (!cat->IsEOF() && cat->inputs.begin() != prev);

    cat->reading = false;
}

static int
istream_cat_as_fd(struct istream *istream)
{
    CatIstream *cat = istream_to_cat(istream);

    /* we can safely forward the as_fd() call to our input if it's the
       last one */

    if (std::next(cat->inputs.begin()) != cat->inputs.end())
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

inline CatIstream::CatIstream(struct pool &p, va_list ap)
{
    istream_init(&output, &istream_cat, &p);

    auto i = inputs.before_begin();

    struct istream *istream;
    while ((istream = va_arg(ap, struct istream *)) != nullptr) {
        assert(!istream_has_handler(istream));

        auto *input = NewFromPool<CatInput>(p);
        i = inputs.insert_after(i, *input);

        input->cat = this;

        istream_assign_handler(&input->istream, istream,
                               &MakeIstreamHandler<CatInput>::handler, input,
                               0);
    }
}

struct istream *
istream_cat_new(struct pool *pool, ...)
{
    va_list ap;
    va_start(ap, pool);
    auto cat = NewFromPool<CatIstream>(*pool, *pool, ap);
    va_end(ap);
    return &cat->output;
}
