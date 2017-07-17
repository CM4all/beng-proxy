/*
 * Global declarations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "bp_instance.hxx"
#include "fb_pool.hxx"
#include "control_server.hxx"
#include "DirectResourceLoader.hxx"
#include "CachedResourceLoader.hxx"
#include "FilterResourceLoader.hxx"
#include "http_cache.hxx"
#include "fcache.hxx"
#include "translation/Cache.hxx"
#include "lhttp_stock.hxx"
#include "fcgi/Stock.hxx"
#include "stock/MapStock.hxx"
#include "session_save.hxx"
#include "event/Duration.hxx"
#include "nfs/Cache.hxx"
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
