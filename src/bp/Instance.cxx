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

#include "Instance.hxx"
#include "Worker.hxx"
#include "Listener.hxx"
#include "Connection.hxx"
#include "fb_pool.hxx"
#include "control/Server.hxx"
#include "control/Local.hxx"
#include "cluster/TcpBalancer.hxx"
#include "pipe_stock.hxx"
#include "DirectResourceLoader.hxx"
#include "CachedResourceLoader.hxx"
#include "FilterResourceLoader.hxx"
#include "BufferedResourceLoader.hxx"
#include "http_cache.hxx"
#include "fcache.hxx"
#include "translation/Stock.hxx"
#include "translation/Cache.hxx"
#include "lhttp_stock.hxx"
#include "fcgi/Stock.hxx"
#include "was/Stock.hxx"
#include "delegate/Stock.hxx"
#include "tcp_stock.hxx"
#include "fs/Stock.hxx"
#include "fs/Balancer.hxx"
#include "stock/MapStock.hxx"
#include "session/Save.hxx"
#include "nfs/Stock.hxx"
#include "nfs/Cache.hxx"
#include "spawn/Client.hxx"
#include "access_log/Glue.hxx"

#include <sys/signal.h>

static constexpr auto COMPRESS_INTERVAL = std::chrono::minutes(10);

BpInstance::BpInstance() noexcept
    :shutdown_listener(event_loop, BIND_THIS_METHOD(ShutdownCallback)),
     sighup_event(event_loop, SIGHUP, BIND_THIS_METHOD(ReloadEventCallback)),
     compress_timer(event_loop, BIND_THIS_METHOD(OnCompressTimer)),
     child_process_registry(event_loop),
     spawn_worker_event(event_loop,
                        BIND_THIS_METHOD(RespawnWorkerCallback)),
     avahi_client(event_loop, "beng-proxy"),
     session_save_timer(event_loop, BIND_THIS_METHOD(SaveSessions))
{
}

BpInstance::~BpInstance() noexcept
{
    delete (BufferedResourceLoader *)buffered_filter_resource_loader;

    if (filter_resource_loader != direct_resource_loader)
        delete (FilterResourceLoader *)filter_resource_loader;

    delete (DirectResourceLoader *)direct_resource_loader;

    FreeStocksAndCaches();
}

void
BpInstance::FreeStocksAndCaches() noexcept
{
    translation_service = nullptr;
    delete std::exchange(translation_cache, nullptr);
    delete std::exchange(translation_stock, nullptr);

    if (http_cache != nullptr) {
        delete (CachedResourceLoader *)cached_resource_loader;
        cached_resource_loader = nullptr;

        http_cache_close(http_cache);
        http_cache = nullptr;
    }

    if (filter_cache != nullptr) {
        filter_cache_close(filter_cache);
        filter_cache = nullptr;
    }

    if (lhttp_stock != nullptr) {
        lhttp_stock_free(lhttp_stock);
        lhttp_stock = nullptr;
    }

    if (fcgi_stock != nullptr) {
        fcgi_stock_free(fcgi_stock);
        fcgi_stock = nullptr;
    }

    if (was_stock != nullptr) {
        was_stock_free(was_stock);
        was_stock = nullptr;
    }

    delete std::exchange(fs_balancer, nullptr);
    delete std::exchange(fs_stock, nullptr);

    delete std::exchange(tcp_balancer, nullptr);

    delete tcp_stock;
    tcp_stock = nullptr;

    if (delegate_stock != nullptr) {
        delegate_stock_free(delegate_stock);
        delegate_stock = nullptr;
    }

    if (nfs_cache != nullptr) {
        nfs_cache_free(nfs_cache);
        nfs_cache = nullptr;
    }

    if (nfs_stock != nullptr) {
        nfs_stock_free(nfs_stock);
        nfs_stock = nullptr;
    }

    delete std::exchange(pipe_stock, nullptr);
}

void
BpInstance::ForkCow(bool inherit) noexcept
{
    fb_pool_fork_cow(inherit);

    if (translation_cache != nullptr)
        translation_cache->ForkCow(inherit);

    if (http_cache != nullptr)
        http_cache_fork_cow(*http_cache, inherit);

    if (filter_cache != nullptr)
        filter_cache_fork_cow(*filter_cache, inherit);

    if (nfs_cache != nullptr)
        nfs_cache_fork_cow(*nfs_cache, inherit);
}

void
BpInstance::Compress() noexcept
{
    fb_pool_compress();
}

void
BpInstance::ScheduleCompress() noexcept
{
    compress_timer.Schedule(COMPRESS_INTERVAL);
}

void
BpInstance::OnCompressTimer() noexcept
{
    Compress();
    ScheduleCompress();
}

void
BpInstance::FadeChildren() noexcept
{
    if (lhttp_stock != nullptr)
        lhttp_stock_fade_all(*lhttp_stock);

    if (fcgi_stock != nullptr)
        fcgi_stock_fade_all(*fcgi_stock);

    if (was_stock != nullptr)
        was_stock->FadeAll();

    if (delegate_stock != nullptr)
        delegate_stock->FadeAll();
}

void
BpInstance::FadeTaggedChildren(const char *tag) noexcept
{
    assert(tag != nullptr);

    if (lhttp_stock != nullptr)
        lhttp_stock_fade_tag(*lhttp_stock, tag);

    if (fcgi_stock != nullptr)
        fcgi_stock_fade_tag(*fcgi_stock, tag);

    if (was_stock != nullptr)
        was_stock_fade_tag(*was_stock, tag);

    // TODO: delegate_stock
}

void
BpInstance::SaveSessions() noexcept
{
    session_save();

    ScheduleSaveSessions();
}

void
BpInstance::ScheduleSaveSessions() noexcept
{
    /* save all sessions every 2 minutes */
    session_save_timer.Schedule(std::chrono::minutes(2));
}
