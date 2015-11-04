/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_iconv.hxx"
#include "FacadeIstream.hxx"
#include "fb_pool.hxx"
#include "SliceFifoBuffer.hxx"

#include <iconv.h>

#include <glib.h>

#include <assert.h>
#include <errno.h>

class IconvIstream final : public FacadeIstream {
    iconv_t iconv;
    SliceFifoBuffer buffer;

public:
    IconvIstream(struct pool &p, Istream &_input,
                 iconv_t _iconv)
        :FacadeIstream(p, _input,
                       MakeIstreamHandler<IconvIstream>::handler, this),
         iconv(_iconv)
    {
    }

    ~IconvIstream() {
        buffer.FreeIfDefined(fb_pool_get());
        iconv_close(iconv);
        iconv = (iconv_t)-1;
    }

    bool IsOpen() const {
        return iconv != (iconv_t)-1;
    }

    /* virtual methods from class Istream */

    off_t _GetAvailable(bool partial) override {
        if (partial)
            return buffer.GetAvailable();

        return -1;
    }

    void _Read() override;
    void _Close() override;

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
    buffer.AllocateIfNull(fb_pool_get());

    const char *src = data;

    do {
        auto w = buffer.Write();
        if (w.IsEmpty()) {
            /* no space left in the buffer: attempt to flush it */

            size_t nbytes = SendFromBuffer(buffer);
            if (nbytes == 0) {
                if (!IsOpen())
                    return 0;
                break;
            }

            assert(IsOpen());

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

                    GError *error = g_error_new_literal(iconv_quark(), 0,
                                                        "incomplete sequence");
                    DestroyError(error);
                    return 0;
                }

                length = 0;
                break;

            case E2BIG:
                /* output buffer is full: flush dest */
                nbytes = SendFromBuffer(buffer);
                if (nbytes == 0) {
                    if (!IsOpen())
                        return 0;

                    /* reset length to 0, to make the loop quit
                       (there's no "double break" to break out of the
                       while loop in C) */
                    length = 0;
                    break;
                }

                assert(IsOpen());
                break;
            }
        }
    } while (length > 0);

    SendFromBuffer(buffer);
    if (!IsOpen())
        return 0;

    buffer.FreeIfEmpty(fb_pool_get());

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

    const ScopePoolRef ref(GetPool() TRACE_ARGS);
    return Feed((const char *)data, length);
}

void
IconvIstream::OnEof()
{
    assert(input.IsDefined());
    input.Clear();

    if (buffer.IsEmpty())
        DestroyEof();
}

void
IconvIstream::OnError(GError *error)
{
    assert(input.IsDefined());

    DestroyError(error);
}

/*
 * istream implementation
 *
 */

void
IconvIstream::_Read()
{
    if (input.IsDefined())
        input.Read();
    else {
        size_t rest = ConsumeFromBuffer(buffer);
        if (rest == 0)
            DestroyEof();
    }
}

void
IconvIstream::_Close()
{
    if (input.IsDefined())
        input.Close();
    Destroy();
}

/*
 * constructor
 *
 */

Istream *
istream_iconv_new(struct pool *pool, Istream &input,
                  const char *tocode, const char *fromcode)
{
    const iconv_t iconv = iconv_open(tocode, fromcode);
    if (iconv == (iconv_t)-1)
        return nullptr;

    return NewIstream<IconvIstream>(*pool, input, iconv);
}
