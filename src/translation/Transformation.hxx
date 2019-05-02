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

#include "FilterTransformation.hxx"
#include "SubstTransformation.hxx"
#include "util/Compiler.h"

#include <new>
#include <type_traits>

class AllocatorPtr;

struct XmlProcessorTransformation {
    unsigned options;
};

struct CssProcessorTransformation {
    unsigned options;
};

struct TextProcessorTransformation {
};

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
        XmlProcessorTransformation processor;

        CssProcessorTransformation css_processor;

        TextProcessorTransformation text_processor;

        FilterTransformation filter;

        SubstTransformation subst;

        /* we don't even try to call destructors in this union, and
           these assertions ensure that this is safe: */
        static_assert(std::is_trivially_destructible<XmlProcessorTransformation>::value);
        static_assert(std::is_trivially_destructible<CssProcessorTransformation>::value);
        static_assert(std::is_trivially_destructible<TextProcessorTransformation>::value);
        static_assert(std::is_trivially_destructible<FilterTransformation>::value);
        static_assert(std::is_trivially_destructible<SubstTransformation>::value);
    } u;

    explicit Transformation(XmlProcessorTransformation &&src) noexcept
        :type(Type::PROCESS) {
        new(&u.processor) XmlProcessorTransformation(std::move(src));
    }

    explicit Transformation(CssProcessorTransformation &&src) noexcept
        :type(Type::PROCESS_CSS) {
        new(&u.processor) CssProcessorTransformation(std::move(src));
    }

    explicit Transformation(TextProcessorTransformation &&src) noexcept
        :type(Type::PROCESS_TEXT) {
        new(&u.processor) TextProcessorTransformation(std::move(src));
    }

    explicit Transformation(FilterTransformation &&src) noexcept
        :type(Type::FILTER) {
        new(&u.filter) FilterTransformation(std::move(src));
    }

    explicit Transformation(SubstTransformation &&src) noexcept
        :type(Type::SUBST) {
        new(&u.subst) SubstTransformation(std::move(src));
    }

    Transformation(AllocatorPtr alloc, const Transformation &src) noexcept;

    Transformation(const Transformation &) = delete;
    Transformation &operator=(const Transformation &) = delete;

    /**
     * Returns true if the chain contains at least one "PROCESS"
     * transformation.
     */
    gcc_pure
    static bool HasProcessor(const Transformation *head) noexcept;

    /**
     * Returns true if the first "PROCESS" transformation in the chain (if
     * any) includes the "CONTAINER" processor option.
     */
    gcc_pure
    static bool IsContainer(const Transformation *head) noexcept;

    /**
     * Does this transformation need to be expanded with
     * transformation_expand()?
     */
    gcc_pure
    bool IsExpandable() const noexcept {
        return type == Type::FILTER &&
            u.filter.IsExpandable();
    }

    /**
     * Does any transformation in the linked list need to be expanded with
     * transformation_expand()?
     */
    gcc_pure
    bool IsChainExpandable() const noexcept;

    gcc_malloc
    Transformation *Dup(AllocatorPtr alloc) const noexcept;

    gcc_malloc
    static Transformation *DupChain(AllocatorPtr alloc,
                                    const Transformation *src) noexcept;

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
