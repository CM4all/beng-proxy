/*
 * Copyright 2007-2018 Content Management AG
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

#ifndef BENG_TRANSFORMATION_HXX
#define BENG_TRANSFORMATION_HXX

#include "ResourceAddress.hxx"

#include "util/Compiler.h"

#include <assert.h>

struct pool;
class AllocatorPtr;

/**
 * Transformations which can be applied to resources.
 */
struct Transformation {
    Transformation *next = nullptr;

    enum class Type {
        PROCESS,
        PROCESS_CSS,
        PROCESS_TEXT,
        FILTER,
        SUBST,
    } type;

    union {
        struct {
            unsigned options;
        } processor;

        struct {
            unsigned options;
        } css_processor;

        struct {
            ResourceAddress address;

            /**
             * Send the X-CM4all-BENG-User header to the filter?
             */
            bool reveal_user;
        } filter;

        struct {
            const char *yaml_file;

            const char *yaml_map_path;
        } subst;
    } u;

    explicit Transformation(Type _type) noexcept
        :type(_type) {}

    Transformation(AllocatorPtr alloc, const Transformation &src) noexcept;

    /**
     * Returns true if the chain contains at least one "PROCESS"
     * transformation.
     */
    gcc_pure
    static bool HasProcessor(const Transformation *head);

    /**
     * Returns true if the first "PROCESS" transformation in the chain (if
     * any) includes the "CONTAINER" processor option.
     */
    gcc_pure
    static bool IsContainer(const Transformation *head);

    /**
     * Does this transformation need to be expanded with
     * transformation_expand()?
     */
    gcc_pure
    bool IsExpandable() const {
        return type == Type::FILTER &&
            u.filter.address.IsExpandable();
    }

    /**
     * Does any transformation in the linked list need to be expanded with
     * transformation_expand()?
     */
    gcc_pure
    bool IsChainExpandable() const;

    gcc_malloc
    Transformation *Dup(AllocatorPtr alloc) const;

    gcc_malloc
    static Transformation *DupChain(AllocatorPtr alloc,
                                    const Transformation *src);

    /**
     * Expand the strings in this transformation (not following the linked
     * lits) with the specified regex result.
     *
     * Throws std::runtime_error on error.
     */
    void Expand(AllocatorPtr alloc, const MatchInfo &match_info);

    /**
     * The same as Expand(), but expand all transformations in the
     * linked list.
     */
    void ExpandChain(AllocatorPtr alloc, const MatchInfo &match_info);
};

#endif
