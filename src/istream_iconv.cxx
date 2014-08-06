/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_iconv.hxx"
#include "istream_buffer.hxx"
#include "pool.hxx"
#include "util/Cast.hxx"
#include "util/ForeignFifoBuffer.hxx"

#include <iconv.h>

#include <assert.h>
#include <errno.h>

struct istream_iconv {
    struct istream output;
    struct istream *input;
    iconv_t iconv;
    ForeignFifoBuffer<uint8_t> buffer;
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
iconv_feed(struct istream_iconv *ic, const char *data, size_t length)
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
                    istream_close_handler(ic->input);
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
    struct istream_iconv *ic = (struct istream_iconv *)ctx;

    assert(ic->input != nullptr);

    const ScopePoolRef ref(*ic->output.pool TRACE_ARGS);
    return iconv_feed(ic, (const char *)data, length);
}

static void
iconv_input_eof(void *ctx)
{
    struct istream_iconv *ic = (struct istream_iconv *)ctx;

    assert(ic->input != nullptr);
    ic->input = nullptr;

    if (ic->buffer.IsEmpty()) {
        ic->buffer.SetNull();
        iconv_close(ic->iconv);
        istream_deinit_eof(&ic->output);
    }
}

static void
iconv_input_abort(GError *error, void *ctx)
{
    struct istream_iconv *ic = (struct istream_iconv *)ctx;

    assert(ic->input != nullptr);

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

static inline struct istream_iconv *
istream_to_iconv(struct istream *istream)
{
    return &ContainerCast2(*istream, &istream_iconv::output);
}

static void
istream_iconv_read(struct istream *istream)
{
    struct istream_iconv *ic = istream_to_iconv(istream);

    if (ic->input != nullptr)
        istream_read(ic->input);
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
    struct istream_iconv *ic = istream_to_iconv(istream);

    ic->buffer.SetNull();

    if (ic->input != nullptr)
        istream_close_handler(ic->input);
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

struct istream *
istream_iconv_new(struct pool *pool, struct istream *input,
                  const char *tocode, const char *fromcode)
{
    struct istream_iconv *ic = istream_new_macro(pool, iconv);

    assert(input != nullptr);
    assert(!istream_has_handler(input));

    ic->iconv = iconv_open(tocode, fromcode);
    if (ic->iconv == (iconv_t)-1) {
        istream_deinit(&ic->output);
        return nullptr;
    }

    constexpr size_t BUFFER_SIZE = 1024;
    ic->buffer.SetBuffer(PoolAlloc<uint8_t>(*pool, BUFFER_SIZE), BUFFER_SIZE);

    istream_assign_handler(&ic->input, input,
                           &iconv_input_handler, ic,
                           0);

    return istream_struct_cast(&ic->output);
}
