/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk.com>
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

#ifndef HTTP_CHUNK_PARSER_HXX
#define HTTP_CHUNK_PARSER_HXX

#include "util/ConstBuffer.hxx"

#include "util/Compiler.h"

#include <algorithm>
#include <stdexcept>

#include <assert.h>
#include <stddef.h>

/**
 * Incremental parser for "Transfer-Encoding:chunked".
 */
class HttpChunkParser {
    enum class State {
        NONE,
        SIZE,
        AFTER_SIZE,
        DATA,
        AFTER_DATA,
        TRAILER,
        TRAILER_DATA,
        END,
    } state = State::NONE;

    size_t remaining_chunk;

public:
    bool HasEnded() const {
        return state == State::END;
    }

    /**
     * Find the next data chunk.
     *
     * Throws exception on error.
     *
     * @return a pointer to the data chunk, an empty chunk pointing to
     * the end of input if there is no data chunk
     */
    ConstBuffer<void> Parse(ConstBuffer<void> input);

    bool Consume(size_t nbytes) {
        assert(nbytes > 0);
        assert(state == State::DATA);
        assert(nbytes <= remaining_chunk);

        remaining_chunk -= nbytes;

        bool finished = remaining_chunk == 0;
        if (finished)
            state = State::AFTER_DATA;
        return finished;
    }

    size_t GetAvailable() const {
        return state == State::DATA
            ? remaining_chunk
            : 0;
    }
};

ConstBuffer<void>
HttpChunkParser::Parse(ConstBuffer<void> _input)
{
    assert(!_input.IsNull());

    const auto input = ConstBuffer<char>::FromVoid(_input);
    auto p = input.begin();
    const auto end = input.end();
    size_t digit;

    while (p != end) {
        assert(p < end);

        const auto ch = *p;
        switch (state) {
        case State::NONE:
        case State::SIZE:
            if (ch >= '0' && ch <= '9') {
                digit = ch - '0';
            } else if (ch >= 'a' && ch <= 'f') {
                digit = ch - 'a' + 0xa;
            } else if (ch >= 'A' && ch <= 'F') {
                digit = ch - 'A' + 0xa;
            } else if (state == State::SIZE) {
                state = State::AFTER_SIZE;
                continue;
            } else {
                throw std::runtime_error("chunk length expected");
            }

            if (state == State::NONE) {
                state = State::SIZE;
                remaining_chunk = 0;
            }

            ++p;
            remaining_chunk = remaining_chunk * 0x10 + digit;
            break;

        case State::AFTER_SIZE:
            if (ch == '\n') {
                if (remaining_chunk == 0)
                    state = State::TRAILER;
                else
                    state = State::DATA;
            }

            ++p;
            break;

        case State::DATA:
            assert(remaining_chunk > 0);

            return {p, std::min(size_t(end - p), remaining_chunk)};

        case State::AFTER_DATA:
            if (ch == '\n') {
                state = State::NONE;
            } else if (ch != '\r') {
                throw std::runtime_error("newline expected");
            }

            ++p;
            break;

        case State::TRAILER:
            ++p;
            if (ch == '\n') {
                state = State::END;
                return {p, 0};
            } else if (ch != '\r') {
                state = State::TRAILER_DATA;
            }
            break;

        case State::TRAILER_DATA:
            ++p;
            if (ch == '\n')
                state = State::TRAILER;
            break;

        case State::END:
            assert(false);
            gcc_unreachable();
        }
    }

    return {p, 0};
}

#endif
