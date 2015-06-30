/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_iconv.hxx"
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

static size_t
iconv_feed(IconvIstream *ic, const char *data, size_t length)
{
    const char *src = data;

    do {
        auto w = ic->buffer.Write();
        if (w.IsEmpty()) {
            /* no space left in the buffer: attempt to flush it */

            size_t nbytes = istream_buffer_send(&ic->output, ic->buffer);
            if (nbytes == 0) {
                if (ic->buffer.IsNull())
                    return 0;
                break;
            }

            assert(ic->buffer.IsDefined());

            continue;
        }

        char *const buffer = (char *)w.data;
        char *dest = buffer;
        size_t dest_left = w.size;

        size_t ret = deconst_iconv(ic->iconv, &src, &length, &dest, &dest_left);
        if (dest > buffer)
            ic->buffer.Append(dest - buffer);

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
                    ic->input.Close();
                    iconv_close(ic->iconv);

                    GError *error = g_error_new_literal(iconv_quark(), 0,
                                                        "incomplete sequence");
                    istream_deinit_abort(&ic->output, error);
                    return 0;
                }

                length = 0;
                break;

            case E2BIG:
                /* output buffer is full: flush dest */
                nbytes = istream_buffer_send(&ic->output, ic->buffer);
                if (nbytes == 0) {
                    if (ic->buffer.IsNull())
                        return 0;

                    /* reset length to 0, to make the loop quit
                       (there's no "double break" to break out of the
                       while loop in C) */
                    length = 0;
                    break;
                }

                assert(ic->buffer.IsDefined());
                break;
            }
        }
    } while (length > 0);

    istream_buffer_send(&ic->output, ic->buffer);
    if (ic->buffer.IsNull())
        return 0;

    return src - data;
}


/*
 * istream handler
 *
 */

static size_t
iconv_input_data(const void *data, size_t length, void *ctx)
{
    IconvIstream *ic = (IconvIstream *)ctx;

    assert(ic->input.IsDefined());

    const ScopePoolRef ref(*ic->output.pool TRACE_ARGS);
    return iconv_feed(ic, (const char *)data, length);
}

static void
iconv_input_eof(void *ctx)
{
    IconvIstream *ic = (IconvIstream *)ctx;

    assert(ic->input.IsDefined());
    ic->input.Clear();

    if (ic->buffer.IsEmpty()) {
        ic->buffer.SetNull();
        iconv_close(ic->iconv);
        istream_deinit_eof(&ic->output);
    }
}

static void
iconv_input_abort(GError *error, void *ctx)
{
    IconvIstream *ic = (IconvIstream *)ctx;

    assert(ic->input.IsDefined());

    ic->buffer.SetNull();

    iconv_close(ic->iconv);
    istream_deinit_abort(&ic->output, error);
}

static const struct istream_handler iconv_input_handler = {
    .data = iconv_input_data,
    .eof = iconv_input_eof,
    .abort = iconv_input_abort,
};


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
    :input(_input, iconv_input_handler, this),
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
