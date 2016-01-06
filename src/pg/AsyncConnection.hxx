/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef ASYNC_PG_CONNECTION_HXX
#define ASYNC_PG_CONNECTION_HXX

#include "Connection.hxx"
#include "event/Event.hxx"

#include <cassert>

class AsyncPgConnectionHandler {
public:
    virtual void OnConnect() = 0;
    virtual void OnDisconnect() = 0;
    virtual void OnNotify(const char *name) = 0;
    virtual void OnError(const char *prefix, const char *error) = 0;
};

/**
 * A PostgreSQL database connection that connects asynchronously,
 * reconnects automatically and provides an asynchronous notify
 * handler.
 */
class AsyncPgConnection : public PgConnection {
    const std::string schema;

    AsyncPgConnectionHandler &handler;

    enum class State {
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

    State state = State::DISCONNECTED;

    /**
     * DISCONNECTED: not used.
     *
     * CONNECTING: used by PollConnect().
     *
     * RECONNECTING: used by PollReconnect().
     *
     * READY: used by PollNotify().
     *
     * WAITING: a timer which reconnects.
     */
    Event event;

public:
    AsyncPgConnection(const char *conninfo, const char *schema,
                      AsyncPgConnectionHandler &handler);

    ~AsyncPgConnection() {
        Disconnect();
    }

    const std::string &GetSchemaName() const {
        return schema;
    }

    bool IsReady() {
        assert(IsDefined());

        return state == State::READY;
    }

    void Reconnect();

    void Disconnect();

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
    void PollNotify();

    void ScheduleReconnect();

private:
    void OnEvent();
    void OnReconnectTimer();
};

#endif
