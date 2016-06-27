/*
 * Web Application Socket protocol, control channel library.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_WAS_CONTROL_HXX
#define BENG_PROXY_WAS_CONTROL_HXX

#include "event/SocketEvent.hxx"
#include "SliceFifoBuffer.hxx"
#include "glibfwd.hxx"

#include <was/protocol.h>

#include <stddef.h>

struct StringMap;
template<typename T> struct ConstBuffer;

class WasControlHandler {
public:
    /**
     * A packet was received.
     *
     * @return false if the object was closed
     */
    virtual bool OnWasControlPacket(enum was_command cmd,
                                    ConstBuffer<void> payload) = 0;

    /**
     * Called after a group of control packets have been handled, and
     * the input buffer is drained.
     *
     * @return false if the #WasControl object has been destructed
     */
    virtual bool OnWasControlDrained() {
        return true;
    }

    virtual void OnWasControlDone() = 0;
    virtual void OnWasControlError(GError *error) = 0;
};

class WasControl {
    int fd;

    bool done = false;

    WasControlHandler &handler;

    SocketEvent read_event, write_event;

    struct {
        unsigned bulk = 0;
    } output;

    SliceFifoBuffer input_buffer, output_buffer;

public:
    WasControl(EventLoop &event_loop, int _fd, WasControlHandler &_handler);

    EventLoop &GetEventLoop() {
        return read_event.GetEventLoop();
    }

    bool IsDefined() const {
        return fd >= 0;
    }

    bool Send(enum was_command cmd,
              const void *payload, size_t payload_length);

    bool SendEmpty(enum was_command cmd) {
        return Send(cmd, nullptr, 0);
    }

    bool SendString(enum was_command cmd, const char *payload);

    bool SendUint64(enum was_command cmd, uint64_t payload) {
        return Send(cmd, &payload, sizeof(payload));
    }

    bool SendArray(enum was_command cmd, ConstBuffer<const char *> values);

    bool SendStrmap(enum was_command cmd, const StringMap &map);

    /**
     * Enables bulk mode.
     */
    void BulkOn() {
        ++output.bulk;
    }

    /**
     * Disables bulk mode and flushes the output buffer.
     */
    bool BulkOff();

    void Done();

    bool IsEmpty() const {
        return input_buffer.IsEmpty() && output_buffer.IsEmpty();
    }

private:
    void *Start(enum was_command cmd, size_t payload_length);
    bool Finish(size_t payload_length);

    void ScheduleRead();
    void ScheduleWrite();

public:
    /**
     * Release the socket held by this object.
     */
    void ReleaseSocket();

private:
    void InvokeDone() {
        ReleaseSocket();
        handler.OnWasControlDone();
    }

    void InvokeError(GError *error) {
        assert(error != nullptr);

        ReleaseSocket();
        handler.OnWasControlError(error);
    }

    bool InvokeDrained() {
        return handler.OnWasControlDrained();
    }

    /**
     * Consume data from the input buffer.  Returns false if this object
     * has been destructed.
     */
    bool ConsumeInput();

    void TryRead();
    bool TryWrite();

    void ReadEventCallback(short events);
    void WriteEventCallback(short events);
};

#endif
