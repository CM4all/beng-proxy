#include "delegate/Glue.hxx"
#include "delegate/Handler.hxx"
#include "delegate/Stock.hxx"
#include "spawn/ChildOptions.hxx"
#include "spawn/Registry.hxx"
#include "spawn/Local.hxx"
#include "stock/MapStock.hxx"
#include "async.hxx"
#include "event/Base.hxx"
#include "event/DeferEvent.hxx"
#include "event/Callback.hxx"
#include "RootPool.hxx"
#include "pool.hxx"

#include <glib.h>

#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

static const char helper_path[] = "./cm4all-beng-proxy-delegate-helper";
static StockMap *delegate_stock;

class MyDelegateHandler final : public DelegateHandler {
    DeferEvent defer;

public:
    MyDelegateHandler()
        :defer(MakeSimpleEventCallback(MyDelegateHandler, Stop), this) {}

    void Stop() {
        hstock_free(delegate_stock);
    }

    void OnDelegateSuccess(int fd) override {
        close(fd);

        defer.Add();
    }

    void OnDelegateError(GError *error) override {
        g_printerr("%s\n", error->message);
        g_error_free(error);

        defer.Add();
    }
};

int main(int argc, char **argv)
{
    struct async_operation_ref my_async_ref;

    if (argc != 2) {
        fprintf(stderr, "usage: run-delegate PATH\n");
        return 1;
    }

    EventBase event_base;
    ChildProcessRegistry child_process_registry;
    child_process_registry.SetVolatile();
    LocalSpawnService spawn_service(child_process_registry);

    RootPool root_pool;
    delegate_stock = delegate_stock_new(spawn_service);
    LinearPool pool(root_pool, "test", 8192);

    ChildOptions child_options;
    child_options.Init();

    MyDelegateHandler handler;
    delegate_stock_open(delegate_stock, pool, helper_path, child_options,
                        argv[1],
                        handler, my_async_ref);

    event_base.Dispatch();
}
