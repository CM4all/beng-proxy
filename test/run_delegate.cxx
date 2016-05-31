#include "delegate/Glue.hxx"
#include "delegate/Handler.hxx"
#include "delegate/Stock.hxx"
#include "spawn/Config.hxx"
#include "spawn/ChildOptions.hxx"
#include "spawn/Registry.hxx"
#include "spawn/Local.hxx"
#include "stock/MapStock.hxx"
#include "async.hxx"
#include "event/Loop.hxx"
#include "event/DeferEvent.hxx"
#include "RootPool.hxx"
#include "pool.hxx"

#include <glib.h>

#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

static const char helper_path[] = "./cm4all-beng-proxy-delegate-helper";
static StockMap *delegate_stock;

class MyDelegateHandler final : public DelegateHandler, DeferEvent {
public:
    MyDelegateHandler(EventLoop &event_loop)
        :DeferEvent(event_loop) {}

    void Stop() {
        delete delegate_stock;
    }

    void OnDelegateSuccess(int fd) override {
        close(fd);

        DeferEvent::Schedule();
    }

    void OnDelegateError(GError *error) override {
        g_printerr("%s\n", error->message);
        g_error_free(error);

        DeferEvent::Schedule();
    }

private:
    /* virtual methods from class DeferEvent */
    void OnDeferred() override {
        Stop();
    }
};

int main(int argc, char **argv)
{
    struct async_operation_ref my_async_ref;

    if (argc != 2) {
        fprintf(stderr, "usage: run-delegate PATH\n");
        return 1;
    }

    SpawnConfig spawn_config;

    EventLoop event_loop;

    ChildProcessRegistry child_process_registry(event_loop);
    child_process_registry.SetVolatile();

    LocalSpawnService spawn_service(spawn_config, child_process_registry);

    RootPool root_pool;
    delegate_stock = delegate_stock_new(event_loop, spawn_service);
    LinearPool pool(root_pool, "test", 8192);

    ChildOptions child_options;

    MyDelegateHandler handler(event_loop);
    delegate_stock_open(delegate_stock, pool, helper_path, child_options,
                        argv[1],
                        handler, my_async_ref);

    event_loop.Dispatch();
}
