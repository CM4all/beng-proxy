/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef FIFO_BUFFER_BIO_HJXX
#define FIFO_BUFFER_BIO_HJXX

#include <stdint.h>

typedef struct bio_st BIO;
template<typename T> class ForeignFifoBuffer;

/**
 * Create an OpenSSL BIO wrapper for a #ForeignFifoBuffer.
 */
BIO *
NewFifoBufferBio(ForeignFifoBuffer<uint8_t> &buffer);

#endif
