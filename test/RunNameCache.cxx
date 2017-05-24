#include "certdb/Config.hxx"
#include "ssl/NameCache.hxx"
#include "event/Loop.hxx"
#include "event/ShutdownListener.hxx"
#include "util/PrintException.hxx"

class Instance final : CertNameCacheHandler {
    EventLoop event_loop;
    ShutdownListener shutdown_listener;
    CertNameCache cache;

public:
    explicit Instance(const CertDatabaseConfig &config)
        :shutdown_listener(event_loop, BIND_THIS_METHOD(OnShutdown)),
         cache(event_loop, config, *this) {
        shutdown_listener.Enable();
        cache.Connect();
    }

    void Dispatch() {
        event_loop.Dispatch();
    }

private:
    void OnShutdown() {
        cache.Disconnect();
    }

    /* virtual methods from CertNameCacheHandler */
    void OnCertModified(const std::string &name, bool deleted) override {
        fprintf(stderr, "%s: %s\n",
                deleted ? "deleted" : "modified",
                name.c_str());
    }
};

int
main(int argc, char **argv)
try {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s CONNINFO\n",
                argv[0]);
        return EXIT_FAILURE;
    }

    CertDatabaseConfig config;
    config.connect = argv[1];

    Instance instance(config);
    instance.Dispatch();

    return EXIT_SUCCESS;
} catch (const std::exception &e) {
    PrintException(e);
    return EXIT_FAILURE;
}
