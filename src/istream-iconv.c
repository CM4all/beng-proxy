/*
 * This istream filter passes one iconv at a time.  This is useful for
 * testing and debugging istream handler implementations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream-buffer.h"

#include <assert.h>
#include <iconv.h>
#include <errno.h>

struct istream_iconv {
    struct istream output;
    istream_t input;
    iconv_t iconv;
    struct fifo_buffer *buffer;
};


static inline size_t
deconst_iconv(iconv_t cd,
              const char **inbuf, size_t *inbytesleft,
              char **outbuf, size_t *outbytesleft)
{
    union {
        const char **in;
        char **out;
    } u;

    u.in = inbuf;
    return iconv(cd, u.out, inbytesleft, outbuf, outbytesleft);
}

static size_t
iconv_feed(struct istream_iconv *ic, const char *data, size_t length)
{
    const char *src = data;
    char *buffer, *dest;
    size_t dest_left, ret, nbytes;

    do {
        buffer = dest = fifo_buffer_write(ic->buffer, &dest_left);
        if (buffer == NULL) {
            /* no space left in the buffer: attempt to flush it */

            nbytes = istream_buffer_send(&ic->output, ic->buffer);
            if (nbytes == 0) {
                if (ic->buffer == NULL)
                    return 0;
                break;
            }

            assert(ic->buffer != NULL);

            continue;
        }

        ret = deconst_iconv(ic->iconv, &src, &length, &dest, &dest_left);
        if (dest > buffer)
            fifo_buffer_append(ic->buffer, dest - buffer);

        if (ret == (size_t)-1) {
            switch (errno) {
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
                    istream_deinit_abort(&ic->output);
                    return 0;
                }

                length = 0;
                break;

            case E2BIG:
                /* output buffer is full: flush dest */
                nbytes = istream_buffer_send(&ic->output, ic->buffer);
                if (nbytes == 0) {
                    if (ic->buffer == NULL)
                        return 0;

                    /* reset length to 0, to make the loop quit
                       (there's no "double break" to break out of the
                       while loop in C) */
                    length = 0;
                    break;
                }

                assert(ic->buffer != NULL);
                break;
            }
        }
    } while (length > 0);

    istream_buffer_send(&ic->output, ic->buffer);
    if (ic->buffer == NULL)
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
    struct istream_iconv *ic = ctx;
    size_t nbytes;

    assert(ic->input != NULL);

    pool_ref(ic->output.pool);
    nbytes = iconv_feed(ic, data, length);
    pool_unref(ic->output.pool);

    return nbytes;
}

static void
iconv_input_eof(void *ctx)
{
    struct istream_iconv *ic = ctx;

    assert(ic->input != NULL);
    ic->input = NULL;

    if (fifo_buffer_empty(ic->buffer)) {
        ic->buffer = NULL;
        iconv_close(ic->iconv);
        istream_deinit_eof(&ic->output);
    }
}

static void
iconv_input_abort(void *ctx)
{
    struct istream_iconv *ic = ctx;

    assert(ic->input != NULL);

    ic->buffer = NULL;

    iconv_close(ic->iconv);
    istream_deinit_abort(&ic->output);
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
istream_to_iconv(istream_t istream)
{
    return (struct istream_iconv *)(((char*)istream) - offsetof(struct istream_iconv, output));
}

static void
istream_iconv_read(istream_t istream)
{
    struct istream_iconv *ic = istream_to_iconv(istream);

    if (ic->input != NULL)
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
istream_iconv_close(istream_t istream)
{
    struct istream_iconv *ic = istream_to_iconv(istream);

    ic->buffer = NULL;

    if (ic->input != NULL)
        istream_close_handler(ic->input);
    iconv_close(ic->iconv);
    istream_deinit_abort(&ic->output);
}

static const struct istream istream_iconv = {
    .read = istream_iconv_read,
    .close = istream_iconv_close,
};


/*
 * constructor
 *
 */


istream_t
istream_iconv_new(pool_t pool, istream_t input,
                  const char *tocode, const char *fromcode)
{
    struct istream_iconv *ic = istream_new_macro(pool, iconv);

    assert(input != NULL);
    assert(!istream_has_handler(input));

    ic->iconv = iconv_open(tocode, fromcode);
    if (ic->iconv == NULL) {
        istream_deinit(&ic->output);
        return NULL;
    }

    ic->buffer = fifo_buffer_new(pool, 1024);

    istream_assign_handler(&ic->input, input,
                           &iconv_input_handler, ic,
                           0);

    return istream_struct_cast(&ic->output);
}
