/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef TRAFO_FRAMEWORK_HXX
#define TRAFO_FRAMEWORK_HXX

#include "system/SetupProcess.hxx"
#include "event/ShutdownListener.hxx"
#include "event/Callback.hxx"
#include "Handler.hxx"
#include "Server.hxx"
#include "Connection.hxx"
#include "Request.hxx"
#include "Response.hxx"
#include "util/Error.hxx"

#include <utility>
#include <type_traits>
#include <iostream>
using std::cerr;
using std::endl;

#include <stdlib.h>

class TrafoFrameworkHandler {
    template<typename H> friend class TrafoFramework;

    TrafoConnection *connection;

protected:
    void SendResponse(TrafoResponse &&response) {
        connection->SendResponse(std::move(response));
        delete this;
    }
};

template<typename H>
class TrafoFramework final : TrafoHandler {
    static_assert(std::is_base_of<TrafoFrameworkHandler, H>::value,
                  "Must be TrafoFrameworkHandler");

    EventBase event_base;
    ShutdownListener shutdown_listener;

    TrafoServer server;

public:
    TrafoFramework()
        :shutdown_listener(OnQuitSignal, this),
         server(*this) {
        SetupProcess();

        shutdown_listener.Enable();
    }

    ~TrafoFramework() {
        shutdown_listener.Disable();
    }

    int Run();

private:
    bool Setup(Error &error);

    void OnQuitSignal() {
        cerr << "quit" << endl;
        event_base.Break();
    }

    static void OnQuitSignal(void *ctx) {
        auto &tf = *(TrafoFramework *)ctx;
        tf.OnQuitSignal();
    }

    virtual void OnTrafoRequest(TrafoConnection &connection,
                                const TrafoRequest &request) override;
};

template<typename H>
void
TrafoFramework<H>::OnTrafoRequest(TrafoConnection &connection,
                                  const TrafoRequest &request)
{
    H *handler = new H();
    handler->connection = &connection;
    handler->OnTrafoRequest(request);
}

template<typename H>
bool
TrafoFramework<H>::Setup(Error &error)
{
    return server.ListenPath("/tmp/trafo-example.socket", error);
}

template<typename H>
int
TrafoFramework<H>::Run()
{
    {
        Error error;
        if (!Setup(error)) {
            cerr << error.GetMessage() << endl;
            return EXIT_FAILURE;
        }
    }

    event_base.Dispatch();

    return EXIT_SUCCESS;
}

template<typename H>
static inline int
RunTrafo()
{
    return TrafoFramework<H>().Run();
}

#endif
