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

#ifndef DESTRUCT_OBSERVER_HXX
#define DESTRUCT_OBSERVER_HXX

#ifdef NDEBUG

class DestructAnchor {};

#else

#include <boost/intrusive/list.hpp>

class DestructAnchor;

/**
 * A debug-only class which observes the destruction of a
 * #DestructAnchor instance.  Once the #DestructAnchor gets destructed
 * and thus inaccessible, the #destructed flag gets set.
 */
class DestructObserver
    : public boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::auto_unlink>> {
    friend class DestructAnchor;

    bool destructed = false;

public:
    explicit DestructObserver(DestructAnchor &anchor);

    operator bool() const {
        return destructed;
    }
};

/**
 * An object which notifies all of its observes about its destruction.
 * In non-debug mode (NDEBUG), this is an empty class without
 * overhead.
 */
class DestructAnchor {
    friend class DestructObserver;

    boost::intrusive::list<DestructObserver,
                           boost::intrusive::constant_time_size<false>> observers;

public:
    ~DestructAnchor() {
        /* tell all observers about it */
        for (auto &i : observers)
            i.destructed = true;
    }
};

inline
DestructObserver::DestructObserver(DestructAnchor &anchor)
{
    anchor.observers.push_front(*this);
}

#endif

#endif
