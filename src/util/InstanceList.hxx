/*
 * Copyright (C) 2016 Max Kellermann <max@duempel.org>
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

#ifndef INSTANCE_LIST_HXX
#define INSTANCE_LIST_HXX

#ifdef NDEBUG

template<typename T>
class WithInstanceList {};

#else

#include <boost/intrusive/list.hpp>

template<typename T>
class WithInstanceList {
    typedef boost::intrusive::list_member_hook<boost::intrusive::link_mode<boost::intrusive::auto_unlink>> SiblingsHook;
    SiblingsHook instance_siblings;

    typedef boost::intrusive::list<WithInstanceList,
                                   boost::intrusive::member_hook<WithInstanceList,
                                                                 WithInstanceList::SiblingsHook,
                                                                 &WithInstanceList::instance_siblings>,
                                   boost::intrusive::constant_time_size<false>> InstanceList;

    static InstanceList instances;

protected:
    WithInstanceList() {
        instances.push_back(*this);
    }

    WithInstanceList(WithInstanceList &&) = delete;
    WithInstanceList &operator=(WithInstanceList &&) = delete;

public:
    ~WithInstanceList() {
        instances.erase(instances.iterator_to(*this));
    }
};

template<typename T>
typename WithInstanceList<T>::InstanceList WithInstanceList<T>::instances;

#endif

#endif
