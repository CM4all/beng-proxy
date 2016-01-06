/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "AsyncConnection.hxx"
#include "event/Callback.hxx"

AsyncPgConnection::AsyncPgConnection(const char *conninfo, const char *_schema,
                                     AsyncPgConnectionHandler &_handler)
    :schema(_schema),
     handler(_handler),
     event(-1, 0, MakeSimpleEventCallback(AsyncPgConnection, OnEvent), this)
{
    StartConnect(conninfo);
    state = State::CONNECTING;
    PollConnect();
}

void
AsyncPgConnection::Error()
{
    assert(state == State::CONNECTING ||
           state == State::RECONNECTING ||
           state == State::READY);

    event.Delete();

    const bool was_connected = state == State::READY;
    state = State::DISCONNECTED;

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
        event.Set(GetSocket(), EV_READ,
                  MakeSimpleEventCallback(AsyncPgConnection, OnEvent), this);
        event.Add();
        break;

    case PGRES_POLLING_WRITING:
        event.Set(GetSocket(), EV_WRITE,
                  MakeSimpleEventCallback(AsyncPgConnection, OnEvent), this);
        event.Add();
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
        event.Set(GetSocket(), EV_READ|EV_PERSIST,
                  MakeSimpleEventCallback(AsyncPgConnection, OnEvent), this);
        event.Add();

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

void
AsyncPgConnection::PollNotify()
{
    assert(IsDefined());
    assert(state == State::READY);

    ConsumeInput();

    PgNotify notify;
    switch (GetStatus()) {
    case CONNECTION_OK:
        while ((notify = GetNextNotify()))
            handler.OnNotify(notify->relname);
        break;

    case CONNECTION_BAD:
        Error();
        break;

    default:
        break;
    }
}

void
AsyncPgConnection::Reconnect()
{
    event.Delete();
    StartReconnect();
    state = State::RECONNECTING;
    PollReconnect();
}

void
AsyncPgConnection::Disconnect()
{
    event.Delete();
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
    event.SetTimer(MakeSimpleEventCallback(AsyncPgConnection, OnReconnectTimer),
                   this);
    event.Add(delay);
}

inline void
AsyncPgConnection::OnEvent()
{
    switch (state) {
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
