/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "AsyncConnection.hxx"

AsyncPgConnection::AsyncPgConnection(EventLoop &event_loop,
                                     const char *_conninfo, const char *_schema,
                                     AsyncPgConnectionHandler &_handler)
    :conninfo(_conninfo), schema(_schema),
     handler(_handler),
     socket_event(event_loop, BIND_THIS_METHOD(OnSocketEvent)),
     reconnect_timer(event_loop, BIND_THIS_METHOD(OnReconnectTimer))
{
}

void
AsyncPgConnection::Error()
{
    assert(state == State::CONNECTING ||
           state == State::RECONNECTING ||
           state == State::READY);

    socket_event.Delete();

    const bool was_connected = state == State::READY;
    state = State::DISCONNECTED;

    if (result_handler != nullptr) {
        auto rh = result_handler;
        result_handler = nullptr;
        rh->OnResultError();
    }

    if (was_connected)
        handler.OnDisconnect();

    ScheduleReconnect();
}

void
AsyncPgConnection::Poll(PostgresPollingStatusType status)
{
    switch (status) {
    case PGRES_POLLING_FAILED:
        handler.OnError("Failed to connect to database", GetErrorMessage());
        Error();
        break;

    case PGRES_POLLING_READING:
        socket_event.Set(GetSocket(), EV_READ);
        socket_event.Add();
        break;

    case PGRES_POLLING_WRITING:
        socket_event.Set(GetSocket(), EV_WRITE);
        socket_event.Add();
        break;

    case PGRES_POLLING_OK:
        if (!schema.empty() &&
            (state == State::CONNECTING || state == State::RECONNECTING) &&
            !SetSchema(schema.c_str())) {
            handler.OnError("Failed to set schema", GetErrorMessage());
            Error();
            break;
        }

        state = State::READY;
        socket_event.Set(GetSocket(), EV_READ|EV_PERSIST);
        socket_event.Add();

        handler.OnConnect();

        /* check the connection status, just in case the handler
           method has done awful things */
        if (state == State::READY &&
            GetStatus() == CONNECTION_BAD)
            Error();
        break;

    case PGRES_POLLING_ACTIVE:
        /* deprecated enum value */
        assert(false);
        break;
    }
}

void
AsyncPgConnection::PollConnect()
{
    assert(IsDefined());
    assert(state == State::CONNECTING);

    Poll(PgConnection::PollConnect());
}

void
AsyncPgConnection::PollReconnect()
{
    assert(IsDefined());
    assert(state == State::RECONNECTING);

    Poll(PgConnection::PollReconnect());
}

inline void
AsyncPgConnection::PollResult()
{
    while (!IsBusy()) {
        auto result = ReceiveResult();
        if (result_handler != nullptr) {
            if (result.IsDefined())
                result_handler->OnResult(std::move(result));
            else {
                auto rh = result_handler;
                result_handler = nullptr;
                rh->OnResultEnd();
            }
        }

        if (!result.IsDefined())
            break;
    }
}

void
AsyncPgConnection::PollNotify()
{
    assert(IsDefined());
    assert(state == State::READY);

    const bool was_idle = IsIdle();

    ConsumeInput();

    PgNotify notify;
    switch (GetStatus()) {
    case CONNECTION_OK:
        PollResult();

        while ((notify = GetNextNotify()))
            handler.OnNotify(notify->relname);

        if (!was_idle && IsIdle())
            handler.OnIdle();
        break;

    case CONNECTION_BAD:
        Error();
        break;

    default:
        break;
    }
}

void
AsyncPgConnection::Connect()
{
    assert(state == State::UNINITIALIZED);

    StartConnect(conninfo.c_str());
    state = State::CONNECTING;
    PollConnect();
}

void
AsyncPgConnection::Reconnect()
{
    assert(state != State::UNINITIALIZED);

    socket_event.Delete();
    StartReconnect();
    state = State::RECONNECTING;
    PollReconnect();
}

void
AsyncPgConnection::Disconnect()
{
    if (state == State::UNINITIALIZED)
        return;

    socket_event.Delete();
    reconnect_timer.Cancel();
    PgConnection::Disconnect();
    state = State::DISCONNECTED;
}

void
AsyncPgConnection::ScheduleReconnect()
{
    /* attempt to reconnect every 10 seconds */
    static constexpr struct timeval delay{ 10, 0 };

    assert(IsDefined());
    assert(state == State::DISCONNECTED);

    state = State::WAITING;
    reconnect_timer.Add(delay);
}

inline void
AsyncPgConnection::OnSocketEvent(unsigned)
{
    switch (state) {
    case State::UNINITIALIZED:
    case State::DISCONNECTED:
    case State::WAITING:
        assert(false);
        gcc_unreachable();

    case State::CONNECTING:
        PollConnect();
        break;

    case State::RECONNECTING:
        PollReconnect();
        break;

    case State::READY:
        PollNotify();
        break;
    }
}

inline void
AsyncPgConnection::OnReconnectTimer()
{
    assert(state == State::WAITING);

    Reconnect();
}
