/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_iconv.hxx"
#include "istream_oo.hxx"
#include "istream_pointer.hxx"
#include "istream_buffer.hxx"
#include "pool.hxx"
#include "util/Cast.hxx"
#include "util/ForeignFifoBuffer.hxx"

#include <iconv.h>

#include <assert.h>
#include <errno.h>

struct IconvIstream {
    static constexpr size_t BUFFER_SIZE = 1024;

    struct istream output;
    IstreamPointer input;
    const iconv_t iconv;
    ForeignFifoBuffer<uint8_t> buffer;

    IconvIstream(struct pool &p, struct istream &_input, iconv_t _iconv);

    /* handler */

    size_t OnData(const void *data, size_t length);

    ssize_t OnDirect(gcc_unused FdType type, gcc_unused int fd,
                     gcc_unused size_t max_length) {
        gcc_unreachable();
    }

    void OnEof();
    void OnError(GError *error);

private:
    size_t Feed(const char *data, size_t length);
};

gcc_const
static GQuark
iconv_quark(void)
{
    return g_quark_from_static_string("iconv");
}

static inline size_t
deconst_iconv(iconv_t cd,
              const char **inbuf, size_t *inbytesleft,
              char **outbuf, size_t *outbytesleft)
{
    char **inbuf2 = const_cast<char **>(inbuf);
    return iconv(cd, inbuf2, inbytesleft, outbuf, outbytesleft);
}

size_t
IconvIstream::Feed(const char *data, size_t length)
{
    const char *src = data;

    do {
        auto w = buffer.Write();
        if (w.IsEmpty()) {
            /* no space left in the buffer: attempt to flush it */

            size_t nbytes = istream_buffer_send(&output, buffer);
            if (nbytes == 0) {
                if (buffer.IsNull())
                    return 0;
                break;
            }

            assert(buffer.IsDefined());

            continue;
        }

        char *const dest0 = (char *)w.data;
        char *dest = dest0;
        size_t dest_left = w.size;

        size_t ret = deconst_iconv(iconv, &src, &length, &dest, &dest_left);
        if (dest > dest0)
            buffer.Append(dest - dest0);

        if (ret == (size_t)-1) {
            switch (errno) {
                size_t nbytes;

            case EILSEQ:
                /* invalid sequence: skip this byte */
                ++src;
                --length;
                break;

            case EINVAL:
                /* incomplete sequence: leave it in the buffer */
                if (src == data) {
                    /* XXX we abort here, because we believe if the
                       incomplete sequence is at the start of the
                       buffer, this might be EOF; we should rather
                       buffer this incomplete sequence and report the
                       caller that we consumed it */
                    input.Close();
                    iconv_close(iconv);

                    GError *error = g_error_new_literal(iconv_quark(), 0,
                                                        "incomplete sequence");
                    istream_deinit_abort(&output, error);
                    return 0;
                }

                length = 0;
                break;

            case E2BIG:
                /* output buffer is full: flush dest */
                nbytes = istream_buffer_send(&output, buffer);
                if (nbytes == 0) {
                    if (buffer.IsNull())
                        return 0;

                    /* reset length to 0, to make the loop quit
                       (there's no "double break" to break out of the
                       while loop in C) */
                    length = 0;
                    break;
                }

                assert(buffer.IsDefined());
                break;
            }
        }
    } while (length > 0);

    istream_buffer_send(&output, buffer);
    if (buffer.IsNull())
        return 0;

    return src - data;
}


/*
 * istream handler
 *
 */

size_t
IconvIstream::OnData(const void *data, size_t length)
{
    assert(input.IsDefined());

    const ScopePoolRef ref(*output.pool TRACE_ARGS);
    return Feed((const char *)data, length);
}

void
IconvIstream::OnEof()
{
    assert(input.IsDefined());
    input.Clear();

    if (buffer.IsEmpty()) {
        buffer.SetNull();
        iconv_close(iconv);
        istream_deinit_eof(&output);
    }
}

void
IconvIstream::OnError(GError *error)
{
    assert(input.IsDefined());

    buffer.SetNull();

    iconv_close(iconv);
    istream_deinit_abort(&output, error);
}

/*
 * istream implementation
 *
 */

static inline IconvIstream *
istream_to_iconv(struct istream *istream)
{
    return &ContainerCast2(*istream, &IconvIstream::output);
}

static void
istream_iconv_read(struct istream *istream)
{
    IconvIstream *ic = istream_to_iconv(istream);

    if (ic->input.IsDefined())
        ic->input.Read();
    else {
        size_t rest = istream_buffer_consume(&ic->output, ic->buffer);
        if (rest == 0) {
            iconv_close(ic->iconv);
            istream_deinit_eof(&ic->output);
        }
    }
}

static void
istream_iconv_close(struct istream *istream)
{
    IconvIstream *ic = istream_to_iconv(istream);

    ic->buffer.SetNull();

    if (ic->input.IsDefined())
        ic->input.Close();
    iconv_close(ic->iconv);
    istream_deinit(&ic->output);
}

static const struct istream_class istream_iconv = {
    .read = istream_iconv_read,
    .close = istream_iconv_close,
};


/*
 * constructor
 *
 */

IconvIstream::IconvIstream(struct pool &p, struct istream &_input,
                           iconv_t _iconv)
    :input(_input, MakeIstreamHandler<IconvIstream>::handler, this),
     iconv(_iconv),
     buffer(PoolAlloc<uint8_t>(p, BUFFER_SIZE), BUFFER_SIZE)
{
    istream_init(&output, &istream_iconv, &p);
}

struct istream *
istream_iconv_new(struct pool *pool, struct istream *input,
                  const char *tocode, const char *fromcode)
{
    assert(input != nullptr);
    assert(!istream_has_handler(input));

    const iconv_t iconv = iconv_open(tocode, fromcode);
    if (iconv == (iconv_t)-1)
        return nullptr;

    auto *ic = NewFromPool<IconvIstream>(*pool, *pool, *input, iconv);
    return &ic->output;
}
