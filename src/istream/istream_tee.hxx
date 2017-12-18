/*
 * Copyright 2007-2017 Content Management AG
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

#ifndef BENG_PROXY_ISTREAM_TEE_HXX
#define BENG_PROXY_ISTREAM_TEE_HXX

struct pool;
class UnusedIstreamPtr;
class Istream;
class EventLoop;

/**
 * Create two new streams fed from one input.
 *
 * Data gets delivered to the first output, then to the second output.
 * Destruction (eof / abort) goes the reverse order: the second output
 * gets destructed first.
 *
 * @param input the istream which is duplicated
 * @param first_weak if true, closes the whole object if only the
 * first output remains
 * @param second_weak if true, closes the whole object if only the
 * second output remains
 */
Istream *
istream_tee_new(struct pool &pool, UnusedIstreamPtr input,
                EventLoop &event_loop,
                bool first_weak, bool second_weak);

Istream &
istream_tee_second(Istream &istream);

/**
 * Schedule an Istream::Read() call.
 */
void
istream_tee_defer_read(Istream &istream) noexcept;

#endif
