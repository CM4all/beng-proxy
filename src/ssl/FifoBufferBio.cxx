/*
 * Copyright 2007-2021 CM4all GmbH
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "FifoBufferBio.hxx"
#include "util/ForeignFifoBuffer.hxx"

#include <openssl/bio.h>
#include <openssl/err.h>

#include <string.h>

struct FifoBufferBio {
	ForeignFifoBuffer<uint8_t> &buffer;
};

static int
fb_new(BIO *b)
{
	BIO_set_init(b, 1);
	return 1;
}

static int
fb_free(BIO *b)
{
	if (b == nullptr)
		return 0;

	auto *fb = (FifoBufferBio *)BIO_get_data(b);
	BIO_set_data(b, nullptr);
	delete fb;

	return 1;
}

static int
fb_read(BIO *b, char *out, int outl)
{
	BIO_clear_retry_flags(b);

	auto &fb = *(FifoBufferBio *)BIO_get_data(b);

	auto r = fb.buffer.Read();
	if (r.empty()) {
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

	if (BIO_test_flags(b, BIO_FLAGS_MEM_RDONLY)) {
		BIOerr(BIO_F_MEM_WRITE, BIO_R_WRITE_TO_READ_ONLY_BIO);
		return -1;
	}

	auto &fb = *(FifoBufferBio *)BIO_get_data(b);

	auto w = fb.buffer.Write();
	if (w.empty()) {
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
	auto &fb = *(FifoBufferBio *)BIO_get_data(b);

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

static BIO_METHOD *fb_method;

static void
InitFifoBufferBio()
{
	fb_method = BIO_meth_new(BIO_get_new_index(), "FIFO buffer");
	BIO_meth_set_write(fb_method, fb_write);
	BIO_meth_set_read(fb_method, fb_read);
	BIO_meth_set_puts(fb_method, fb_puts);
	BIO_meth_set_gets(fb_method, fb_gets);
	BIO_meth_set_ctrl(fb_method, fb_ctrl);
	BIO_meth_set_create(fb_method, fb_new);
	BIO_meth_set_destroy(fb_method, fb_free);
}

BIO *
NewFifoBufferBio(ForeignFifoBuffer<uint8_t> &buffer)
{
	if (fb_method == nullptr)
		InitFifoBufferBio();

	BIO *b = BIO_new(fb_method);
	BIO_set_data(b, new FifoBufferBio{buffer});
	return b;
}

void
DeinitFifoBufferBio()
{
	if (fb_method != nullptr)
		BIO_meth_free(std::exchange(fb_method, nullptr));
}
