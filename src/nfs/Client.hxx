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

#ifndef BENG_PROXY_NFS_CLIENT_HXX
#define BENG_PROXY_NFS_CLIENT_HXX

#include <stdint.h>
#include <stddef.h>

struct pool;
class NfsClient;
class NfsClientHandler;
class NfsClientOpenFileHandler;
class NfsClientReadFileHandler;
class NfsFileHandle;
class HttpResponseHandler;
class CancellablePointer;
class EventLoop;

void
nfs_client_new(EventLoop &event_loop,
               const char *server, const char *root,
               NfsClientHandler &handler,
               CancellablePointer &cancel_ptr);

void
nfs_client_free(NfsClient *client);

void
nfs_client_open_file(NfsClient &client,
                     const char *path,
                     NfsClientOpenFileHandler &handler,
                     CancellablePointer &cancel_ptr);

void
nfs_client_close_file(NfsFileHandle &handle);

void
nfs_client_read_file(NfsFileHandle &handle,
                     uint64_t offset, size_t length,
                     NfsClientReadFileHandler &handler);

#endif
