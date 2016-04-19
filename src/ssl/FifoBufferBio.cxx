/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "FifoBufferBio.hxx"
#include "util/ForeignFifoBuffer.hxx"

#include <openssl/bio.h>
#include <openssl/err.h>

#include <string.h>

#define BIO_TYPE_FIFO_BUFFER (43|BIO_TYPE_SOURCE_SINK)

struct FifoBufferBio {
    ForeignFifoBuffer<uint8_t> &buffer;
};

static int
fb_new(BIO *b)
{
    b->init = 1;
    b->num = 0;
    b->ptr = nullptr;
    return 1;
}

static int
fb_free(BIO *b)
{
    if (b == nullptr)
        return 0;

    auto *fb = (FifoBufferBio *)b->ptr;
    b->ptr = nullptr;
    delete fb;

    b->num = -1;
    return 1;
}

static int
fb_read(BIO *b, char *out, int outl)
{
    BIO_clear_retry_flags(b);

    auto &fb = *(FifoBufferBio *)b->ptr;

    auto r = fb.buffer.Read();
    if (r.IsEmpty()) {
        BIO_set_retry_read(b);
        return -1;
    }

    if (outl <= 0)
        return outl;

    size_t nbytes = std::min(r.size, size_t(outl));
    if (out != nullptr) {
        memcpy(out, r.data, nbytes);
        fb.buffer.Consume(nbytes);
    }

    return nbytes;
}

static int
fb_write(BIO *b, const char *in, int inl)
{
    BIO_clear_retry_flags(b);

    if (in == nullptr) {
        BIOerr(BIO_F_MEM_WRITE, BIO_R_NULL_PARAMETER);
        return -1;
    }

    if (inl < 0) {
        BIOerr(BIO_F_MEM_WRITE, BIO_R_INVALID_ARGUMENT);
        return -1;
    }

    if (b->flags & BIO_FLAGS_MEM_RDONLY) {
        BIOerr(BIO_F_MEM_WRITE, BIO_R_WRITE_TO_READ_ONLY_BIO);
        return -1;
    }

    auto &fb = *(FifoBufferBio *)b->ptr;

    auto w = fb.buffer.Write();
    if (w.IsEmpty()) {
        BIO_set_retry_write(b);
        return -1;
    }

    size_t nbytes = std::min(w.size, size_t(inl));
    memcpy(w.data, in, nbytes);
    fb.buffer.Append(nbytes);
    return nbytes;
}

static long
fb_ctrl(BIO *b, int cmd, gcc_unused long num, gcc_unused void *ptr)
{
    auto &fb = *(FifoBufferBio *)b->ptr;

    switch(cmd) {
    case BIO_CTRL_EOF:
        return -1;

    case BIO_CTRL_PENDING:
        return fb.buffer.GetAvailable();

    case BIO_CTRL_FLUSH:
        return 1;

    default:
        return 0;
    }
}

static int
fb_gets(BIO *b, char *buf, int size)
{
    (void)b;
    (void)buf;
    (void)size;

    /* not implemented; I suppose we don't need it */
    assert(false);
    gcc_unreachable();
}

static int
fb_puts(BIO *b, const char *str)
{
    (void)b;
    (void)str;

    /* not implemented; I suppose we don't need it */
    assert(false);
    gcc_unreachable();
}

static BIO_METHOD fb_method = {
    .type = BIO_TYPE_FIFO_BUFFER,
    .name = "FIFO buffer",
    .bwrite = fb_write,
    .bread = fb_read,
    .bputs = fb_puts,
    .bgets = fb_gets,
    .ctrl = fb_ctrl,
    .create = fb_new,
    .destroy = fb_free,
    .callback_ctrl = nullptr,
};

BIO *
NewFifoBufferBio(ForeignFifoBuffer<uint8_t> &buffer)
{
    BIO *b = BIO_new(&fb_method);
    b->ptr = new FifoBufferBio{buffer};
    return b;
}
