/*
 * Global declarations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "bp_instance.hxx"
#include "fb_pool.hxx"
#include "DirectResourceLoader.hxx"
#include "CachedResourceLoader.hxx"
#include "FilterResourceLoader.hxx"
#include "http_cache.hxx"
#include "fcache.hxx"
#include "tcache.hxx"
#include "lhttp_stock.hxx"
#include "fcgi/Stock.hxx"
#include "stock/MapStock.hxx"

#ifdef HAVE_LIBNFS
#include "nfs_cache.hxx"
#endif

#include <sys/signal.h>

BpInstance::BpInstance()
    :shutdown_listener(event_loop, BIND_THIS_METHOD(ShutdownCallback)),
     sighup_event(event_loop, SIGHUP, BIND_THIS_METHOD(ReloadEventCallback)),
     child_process_registry(event_loop),
     spawn_worker_event(event_loop,
                        BIND_THIS_METHOD(RespawnWorkerCallback))
{
}

BpInstance::~BpInstance()
{
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
        filter_cache_fork_cow(filter_cache, inherit);

#ifdef HAVE_LIBNFS
    if (nfs_cache != nullptr)
        nfs_cache_fork_cow(*nfs_cache, inherit);
#endif
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
