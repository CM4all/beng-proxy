/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef ASYNC_PG_CONNECTION_HXX
#define ASYNC_PG_CONNECTION_HXX

#include "Connection.hxx"
#include "event/SocketEvent.hxx"
#include "event/TimerEvent.hxx"

#include <cassert>

class AsyncPgConnectionHandler {
public:
    virtual void OnConnect() = 0;

    /**
     * Called when the connection becomes idle, i.e. ready for a query
     * after the previous query result was finished.  It is not called
     * when the connection becomes idle for the first time after the
     * connection has been established.
     */
    virtual void OnIdle() {}

    virtual void OnDisconnect() = 0;
    virtual void OnNotify(const char *name) = 0;
    virtual void OnError(const char *prefix, const char *error) = 0;
};

class AsyncPgResultHandler {
public:
    virtual void OnResult(PgResult &&result) = 0;
    virtual void OnResultEnd() = 0;
    virtual void OnResultError() {
        OnResultEnd();
    }
};

/**
 * A PostgreSQL database connection that connects asynchronously,
 * reconnects automatically and provides an asynchronous notify
 * handler.
 */
class AsyncPgConnection : public PgConnection {
    const std::string conninfo;
    const std::string schema;

    AsyncPgConnectionHandler &handler;

    enum class State {
         /**
          * The PgConnection has not been initialized yet.  Call Connect().
          */
        UNINITIALIZED,

        /**
         * No database connection exists.
         */
        DISCONNECTED,

        /**
         * Connecting to the database asynchronously.
         */
        CONNECTING,

        /**
         * Reconnecting to the database asynchronously.
         */
        RECONNECTING,

        /**
         * Connection is ready to be used.  As soon as the socket
         * becomes readable, notifications will be received and
         * forwarded to AsyncPgConnectionHandler::OnNotify().
         */
        READY,

        /**
         * Waiting to reconnect.  A timer was scheduled to do this.
         */
        WAITING,
    };

    State state = State::UNINITIALIZED;

    /**
     * DISCONNECTED: not used.
     *
     * CONNECTING: used by PollConnect().
     *
     * RECONNECTING: used by PollReconnect().
     *
     * READY: used by PollNotify().
     *
     * WAITING: not used.
     */
    SocketEvent socket_event;

    /**
     * A timer which reconnects during State::WAITING.
     */
    TimerEvent reconnect_timer;

    AsyncPgResultHandler *result_handler = nullptr;

public:
    /**
     * Construct the object, but do not initiate the connect yet.
     * Call Connect() to do that.
     */
    AsyncPgConnection(EventLoop &event_loop,
                      const char *conninfo, const char *schema,
                      AsyncPgConnectionHandler &handler);

    ~AsyncPgConnection() {
        Disconnect();
    }

    const std::string &GetSchemaName() const {
        return schema;
    }

    bool IsReady() const {
        assert(IsDefined());

        return state == State::READY;
    }

    /**
     * Initiate the initial connect.  This may be called only once.
     */
    void Connect();

    void Reconnect();

    void Disconnect();

    /**
     * Returns true when no asynchronous query is in progress.  In
     * this case, SendQuery() may be called.
     */
    gcc_pure
    bool IsIdle() const {
        assert(IsDefined());

        return state == State::READY && result_handler == nullptr;
    }

    template<typename... Params>
    void SendQuery(AsyncPgResultHandler &_handler, Params... params) {
        assert(IsIdle());

        result_handler = &_handler;

        PgConnection::SendQuery(params...);
    }

    void CheckNotify() {
        if (IsDefined() && IsReady())
            PollNotify();
    }

protected:
    /**
     * This method is called when an fatal error on the connection has
     * occurred.  It will set the state to DISCONNECTED, notify the
     * handler, and schedule a reconnect.
     */
    void Error();

    void Poll(PostgresPollingStatusType status);

    void PollConnect();
    void PollReconnect();
    void PollResult();
    void PollNotify();

    void ScheduleReconnect();

private:
    void OnSocketEvent(unsigned events);
    void OnReconnectTimer();
};

#endif
