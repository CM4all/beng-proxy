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

#ifndef BENG_PROXY_WAS_LAUNCH_HXX
#define BENG_PROXY_WAS_LAUNCH_HXX

#include "io/UniqueFileDescriptor.hxx"
#include "net/UniqueSocketDescriptor.hxx"

class SpawnService;
class ExitListener;
struct ChildOptions;
template<typename T> struct ConstBuffer;

struct WasProcess {
    int pid;
    UniqueSocketDescriptor control;
    UniqueFileDescriptor input, output;

    void Close() {
        control.Close();
        input.Close();
        output.Close();
    }
};

/**
 * Launch WAS child processes.
 *
 * Throws std::runtime_error on error.
 */
WasProcess
was_launch(SpawnService &spawn_service,
           const char *name,
           const char *executable_path,
           ConstBuffer<const char *> args,
           const ChildOptions &options,
           UniqueFileDescriptor stderr_fd,
           ExitListener *listener);

#endif
