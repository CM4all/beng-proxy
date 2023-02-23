// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "FifoBufferBio.hxx"
#include "util/Compiler.h"
#include "util/ForeignFifoBuffer.hxx"

#include <openssl/bio.h>
#include <openssl/err.h>

#include <string.h>

struct FifoBufferBio {
	ForeignFifoBuffer<std::byte> &buffer;
};

static int
fb_new(BIO *b) noexcept
{
	BIO_set_init(b, 1);
	return 1;
}

static int
fb_free(BIO *b) noexcept
{
	if (b == nullptr)
		return 0;

	auto *fb = (FifoBufferBio *)BIO_get_data(b);
	BIO_set_data(b, nullptr);
	delete fb;

	return 1;
}

static int
fb_read(BIO *b, char *out, int outl) noexcept
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

	size_t nbytes = std::min(r.size(), size_t(outl));
	if (out != nullptr) {
		memcpy(out, r.data(), nbytes);
		fb.buffer.Consume(nbytes);
	}

	return nbytes;
}

static int
fb_write(BIO *b, const char *in, int inl) noexcept
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

	const std::size_t nbytes = fb.buffer.MoveFrom(std::span{(const std::byte *)in, std::size_t(inl)});
	if (nbytes == 0) {
		BIO_set_retry_write(b);
		return -1;
	}

	return nbytes;
}

static long
fb_ctrl(BIO *b, int cmd, [[maybe_unused]] long num,
	[[maybe_unused]] void *ptr) noexcept
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
fb_gets(BIO *b, char *buf, int size) noexcept
{
	(void)b;
	(void)buf;
	(void)size;

	/* not implemented; I suppose we don't need it */
	assert(false);
	gcc_unreachable();
}

static int
fb_puts(BIO *b, const char *str) noexcept
{
	(void)b;
	(void)str;

	/* not implemented; I suppose we don't need it */
	assert(false);
	gcc_unreachable();
}

static BIO_METHOD *fb_method;

static void
InitFifoBufferBio() noexcept
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
NewFifoBufferBio(ForeignFifoBuffer<std::byte> &buffer) noexcept
{
	if (fb_method == nullptr)
		InitFifoBufferBio();

	BIO *b = BIO_new(fb_method);
	BIO_set_data(b, new FifoBufferBio{buffer});
	return b;
}

void
DeinitFifoBufferBio() noexcept
{
	if (fb_method != nullptr)
		BIO_meth_free(std::exchange(fb_method, nullptr));
}
