/*
 * Global declarations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "bp_instance.hxx"
#include "fb_pool.hxx"
#include "control_server.hxx"
#include "balancer.hxx"
#include "tcp_balancer.hxx"
#include "pipe_stock.hxx"
#include "DirectResourceLoader.hxx"
#include "CachedResourceLoader.hxx"
#include "FilterResourceLoader.hxx"
#include "http_cache.hxx"
#include "fcache.hxx"
#include "translation/Stock.hxx"
#include "translation/Cache.hxx"
#include "lhttp_stock.hxx"
#include "fcgi/Stock.hxx"
#include "stock/MapStock.hxx"
#include "session_save.hxx"
#include "event/Duration.hxx"
#include "nfs/Stock.hxx"
#include "nfs/Cache.hxx"
#include "memcached/memcached_stock.hxx"
#include "access_log/Glue.hxx"

#include <sys/signal.h>

static constexpr auto &COMPRESS_INTERVAL = EventDuration<600>::value;

BpInstance::BpInstance()
    :shutdown_listener(event_loop, BIND_THIS_METHOD(ShutdownCallback)),
     sighup_event(event_loop, SIGHUP, BIND_THIS_METHOD(ReloadEventCallback)),
     compress_timer(event_loop, BIND_THIS_METHOD(OnCompressTimer)),
     child_process_registry(event_loop),
     spawn_worker_event(event_loop,
                        BIND_THIS_METHOD(RespawnWorkerCallback)),
     avahi_client(event_loop, "beng-proxy"),
     session_save_timer(event_loop, BIND_THIS_METHOD(SaveSesssions))
{
}

BpInstance::~BpInstance()
{
    if (filter_resource_loader != direct_resource_loader)
        delete (FilterResourceLoader *)filter_resource_loader;

    delete (CachedResourceLoader *)cached_resource_loader;
    delete (DirectResourceLoader *)direct_resource_loader;

    FreeStocksAndCaches();
}

void
BpInstance::FreeStocksAndCaches()
{
    if (translate_cache != nullptr) {
        translate_cache_close(translate_cache);
        translate_cache = nullptr;
    }

    if (translate_stock != nullptr) {
        tstock_free(translate_stock);
        translate_stock = nullptr;
    }

    if (http_cache != nullptr) {
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

    delete was_stock;
    was_stock = nullptr;

    if (memcached_stock != nullptr) {
        memcached_stock_free(memcached_stock);
        memcached_stock = nullptr;
    }

    if (tcp_balancer != nullptr) {
        tcp_balancer_free(tcp_balancer);
        tcp_balancer = nullptr;
    }

    delete tcp_stock;
    tcp_stock = nullptr;

    if (balancer != nullptr) {
        balancer_free(balancer);
        balancer = nullptr;
    }

    delete delegate_stock;
    delegate_stock = nullptr;

    if (nfs_cache != nullptr) {
        nfs_cache_free(nfs_cache);
        nfs_cache = nullptr;
    }

    if (nfs_stock != nullptr) {
        nfs_stock_free(nfs_stock);
        nfs_stock = nullptr;
    }

    if (pipe_stock != nullptr) {
        pipe_stock_free(pipe_stock);
        pipe_stock = nullptr;
    }
}

void
BpInstance::ForkCow(bool inherit)
{
    fb_pool_fork_cow(inherit);

    if (translate_cache != nullptr)
        translate_cache_fork_cow(*translate_cache, inherit);

    if (http_cache != nullptr)
        http_cache_fork_cow(*http_cache, inherit);

    if (filter_cache != nullptr)
        filter_cache_fork_cow(*filter_cache, inherit);

    if (nfs_cache != nullptr)
        nfs_cache_fork_cow(*nfs_cache, inherit);
}

void
BpInstance::Compress()
{
    fb_pool_compress();
}

void
BpInstance::ScheduleCompress()
{
    compress_timer.Add(COMPRESS_INTERVAL);
}

void
BpInstance::OnCompressTimer()
{
    Compress();
    ScheduleCompress();
}

void
BpInstance::FadeChildren()
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
BpInstance::SaveSesssions()
{
    session_save();

    ScheduleSaveSessions();
}

void
BpInstance::ScheduleSaveSessions()
{
    /* save all sessions every 2 minutes */
    session_save_timer.Add(EventDuration<120, 0>::value);
}
