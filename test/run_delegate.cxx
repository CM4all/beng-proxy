#include "delegate/Glue.hxx"
#include "delegate/Handler.hxx"
#include "delegate/Stock.hxx"
#include "spawn/Config.hxx"
#include "spawn/ChildOptions.hxx"
#include "spawn/Registry.hxx"
#include "spawn/Local.hxx"
#include "stock/MapStock.hxx"
#include "event/DeferEvent.hxx"
#include "PInstance.hxx"
#include "pool.hxx"
#include "util/Cancellable.hxx"

#include <glib.h>

#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

static const char helper_path[] = "./delegate-helper";
static StockMap *delegate_stock;

class MyDelegateHandler final : public DelegateHandler {
    DeferEvent defer_stop;

public:
    MyDelegateHandler(EventLoop &event_loop)
        :defer_stop(event_loop, BIND_THIS_METHOD(Stop)) {}

    void Stop() {
        delete delegate_stock;
    }

    void OnDelegateSuccess(int fd) override {
        close(fd);

        defer_stop.Schedule();
    }

    void OnDelegateError(GError *error) override {
        g_printerr("%s\n", error->message);
        g_error_free(error);

        defer_stop.Schedule();
    }
};

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: run-delegate PATH\n");
        return 1;
    }

    SpawnConfig spawn_config;

    PInstance instance;

    ChildProcessRegistry child_process_registry(instance.event_loop);
    child_process_registry.SetVolatile();

    LocalSpawnService spawn_service(spawn_config, child_process_registry);

    delegate_stock = delegate_stock_new(instance.event_loop, spawn_service);
    LinearPool pool(instance.root_pool, "test", 8192);

    ChildOptions child_options;

    MyDelegateHandler handler(instance.event_loop);
    CancellablePointer cancel_ptr;
    delegate_stock_open(delegate_stock, pool, helper_path, child_options,
                        argv[1],
                        handler, cancel_ptr);

    instance.event_loop.Dispatch();
}
