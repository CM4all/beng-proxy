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

#include "istream/Pointer.hxx"
#include "util/PrintException.hxx"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

struct StdioSink final : IstreamHandler {
    IstreamPointer input;

    explicit StdioSink(Istream &_input)
        :input(_input, *this) {}

    void LoopRead() {
        while (input.IsDefined())
            input.Read();
    }

    /* virtual methods from class IstreamHandler */

    size_t OnData(const void *data, size_t length) override;

    void OnEof() noexcept override {
        input.Clear();
    }

    void OnError(std::exception_ptr ep) noexcept override {
        input.Clear();

        PrintException(ep);
    }
};

size_t
StdioSink::OnData(const void *data, size_t length)
{
    ssize_t nbytes = write(STDOUT_FILENO, data, length);
    if (nbytes < 0) {
        perror("failed to write to stdout");
        input.ClearAndClose();
        return 0;
    }

    if (nbytes == 0) {
        fprintf(stderr, "failed to write to stdout\n");
        input.ClearAndClose();
        return 0;
    }

    return (size_t)nbytes;
}
