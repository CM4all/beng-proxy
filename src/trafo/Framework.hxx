/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef TRAFO_FRAMEWORK_HXX
#define TRAFO_FRAMEWORK_HXX

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

#include <sys/prctl.h>
#include <signal.h>
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

    class QuitHandler {
        EventBase &base;

    public:
        QuitHandler(EventBase &_base):base(_base) {}

        void operator()() {
            cerr << "quit" << endl;
            base.Break();
        }
    };

    EventBase event_base;
    QuitHandler quit_handler;
    SignalEvent sigterm_event, sigint_event, sigquit_event;

    TrafoServer server;

public:
    TrafoFramework()
        :quit_handler(event_base),
         sigterm_event(SIGTERM, quit_handler),
         sigint_event(SIGINT, quit_handler),
         sigquit_event(SIGQUIT, quit_handler),
         server(*this) {
        /* timer slack 500ms - we don't care for timer correctness */
        prctl(PR_SET_TIMERSLACK, 500000000, 0, 0, 0);

        signal(SIGPIPE, SIG_IGN);
    }

    int Run();

private:
    bool Setup(Error &error);

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
