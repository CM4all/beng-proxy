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

#ifndef BENG_PROXY_SUFFIX_REGISTRY_HXX
#define BENG_PROXY_SUFFIX_REGISTRY_HXX

#include <exception>

struct pool;
struct tcache;
class CancellablePointer;
template<typename T> struct ConstBuffer;
struct Transformation;

class SuffixRegistryHandler {
public:
    /**
     * @param transformations an optional #Transformation chain for
     * all files of this type
     */
    virtual void OnSuffixRegistrySuccess(const char *content_type,
                                         const Transformation *transformations) = 0;

    virtual void OnSuffixRegistryError(std::exception_ptr ep) = 0;
};

/**
 * Interface for Content-Types managed by the translation server.
 */
void
suffix_registry_lookup(struct pool &pool,
                       struct tcache &tcache,
                       ConstBuffer<void> payload,
                       const char *suffix,
                       SuffixRegistryHandler &handler,
                       CancellablePointer &cancel_ptr);

#endif
