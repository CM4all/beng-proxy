#include "avahi/Check.hxx"
#include "avahi/Client.hxx"
#include "avahi/Explorer.hxx"
#include "avahi/ExplorerListener.hxx"
#include "event/Loop.hxx"
#include "event/ShutdownListener.hxx"
#include "net/ToString.hxx"
#include "util/PrintException.hxx"

class Instance final : AvahiServiceExplorerListener {
    EventLoop event_loop;
    ShutdownListener shutdown_listener;
    MyAvahiClient client;
    AvahiServiceExplorer explorer;

public:
    explicit Instance(const char *service)
        :shutdown_listener(event_loop, BIND_THIS_METHOD(OnShutdown)),
         client(event_loop, "RunAvahiExplorer"),
         explorer(client, *this,
                  AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC,
                  service, nullptr) {
        shutdown_listener.Enable();
    }

    void Dispatch() {
        event_loop.Dispatch();
    }

private:
    void OnShutdown() {
        event_loop.Break();
    }

    /* virtual methods from class AvahiServiceExplorerListener */
    void OnAvahiNewObject(const std::string &key,
                          SocketAddress address) override {
        char buffer[1024];
        ToString(buffer, sizeof(buffer), address);

        printf("new '%s' at %s\n", key.c_str(), buffer);
    }

    void OnAvahiRemoveObject(const std::string &key) override {
        printf("remove '%s'\n", key.c_str());
    }
};

int
main(int argc, char **argv)
try {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s SERVICE\n", argv[0]);
        return EXIT_FAILURE;
    }

    const auto service_type = MakeZeroconfServiceType(argv[1], "_tcp");

    Instance instance(service_type.c_str());
    instance.Dispatch();

    return EXIT_SUCCESS;
} catch (const std::exception &e) {
    PrintException(e);
    return EXIT_FAILURE;
}
