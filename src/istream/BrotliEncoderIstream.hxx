// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

struct pool;
class UnusedIstreamPtr;
class ThreadQueue;

struct BrotliEncoderParams {
	/**
	 * Set BROTLI_MODE_TEXT.
	 */
	bool text_mode = false;
};

/**
 * An #Istream filter which compresses data on-the-fly with Brotli.
 */
UnusedIstreamPtr
NewBrotliEncoderIstream(struct pool &pool, ThreadQueue &queue,
			UnusedIstreamPtr input,
			BrotliEncoderParams params={}) noexcept;
