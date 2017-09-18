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

/*
 * Load balancer for AddressList.
 */

#ifndef BENG_PROXY_BALANCER_HXX
#define BENG_PROXY_BALANCER_HXX

#include "StickyHash.hxx"

struct Balancer;
struct AddressList;
class EventLoop;
class SocketAddress;

Balancer *
balancer_new(EventLoop &event_loop);

void
balancer_free(Balancer *balancer);

/**
 * Gets the next socket address to connect to.  These are selected in
 * a round-robin fashion, which results in symmetric load-balancing.
 * If a server is known to be faulty, it is not used (see failure.h).
 *
 * @param session a portion of the session id used to select an
 * address if stickiness is enabled; 0 if there is no session
 */
SocketAddress
balancer_get(Balancer &balancer, const AddressList &list,
             sticky_hash_t sticky_hash);

void
balancer_event_add(Balancer &balancer);

void
balancer_event_del(Balancer &balancer);

#endif
