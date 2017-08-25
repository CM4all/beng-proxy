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

#ifndef STRING_SET_HXX
#define STRING_SET_HXX

#include "util/Compiler.h"

#include <boost/intrusive/slist.hpp>

#include <stddef.h>

class AllocatorPtr;

/**
 * An unordered set of strings.
 */
class StringSet {
    struct Item : boost::intrusive::slist_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>> {
        const char *value;
    };

    typedef boost::intrusive::slist<Item,
                                    boost::intrusive::constant_time_size<false>> List;

    List list;

public:
    StringSet() = default;
    StringSet(const StringSet &) = delete;
    StringSet(StringSet &&) = default;
    StringSet &operator=(const StringSet &) = delete;
    StringSet &operator=(StringSet &&src) = default;

    void Init() {
        list.clear();
    }

    gcc_pure
    bool IsEmpty() const {
        return list.empty();
    }

    gcc_pure
    bool Contains(const char *p) const;

    /**
     * Add a string to the set.  It does not check whether the string
     * already exists.
     *
     * @param p the string value which must be allocated by the caller
     * @param alloc an allocator that is used to allocate the node
     * (not the value)
     */
    void Add(AllocatorPtr alloc, const char *p);

    /**
     * Copy all strings from one set to this, creating duplicates of
     * all values from the specified allocator.
     */
    void CopyFrom(AllocatorPtr alloc, const StringSet &s);

    class const_iterator {
        List::const_iterator i;

    public:
        const_iterator(List::const_iterator _i):i(_i) {}

        bool operator!=(const const_iterator &other) const {
            return i != other.i;
        }

        const char *operator*() const {
            return i->value;
        }

        const_iterator &operator++() {
            ++i;
            return *this;
        }
    };

    const_iterator begin() const {
        return list.begin();
    }

    const_iterator end() const {
        return list.end();
    }
};

#endif
