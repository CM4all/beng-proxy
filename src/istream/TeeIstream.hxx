/*
 * Copyright 2007-2019 Content Management AG
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

#pragma once

struct pool;
class UnusedIstreamPtr;
class Istream;
class EventLoop;

/**
 * An #Istream implementation which copies its input to one or more
 * outputs.
 *
 * Data gets delivered to the first output, then to the second output
 * and so on.  Destruction (eof / abort) goes the reverse order: the
 * last output gets destructed first.
 *
 * @param input the istream which is duplicated
 * @param weak if true, closes the whole object if only this output
 * (and possibly other "weak" outputs) remains
 * @param defer_read schedule a deferred Istream::Read() call
 */
UnusedIstreamPtr
NewTeeIstream(struct pool &pool, UnusedIstreamPtr input,
              EventLoop &event_loop,
              bool weak,
              bool defer_read=false) noexcept;

/**
 * Create another output for the given #TeeIstream.
 */
UnusedIstreamPtr
AddTeeIstream(UnusedIstreamPtr &tee, bool weak) noexcept;
