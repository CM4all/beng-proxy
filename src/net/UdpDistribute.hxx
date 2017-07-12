/*
 * Distribute UDP (control) packets to all workers.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef UDP_DISTRIBUTE_HXX
#define UDP_DISTRIBUTE_HXX

#include "net/UniqueSocketDescriptor.hxx"
#include "event/SocketEvent.hxx"

#include <boost/intrusive/list.hpp>

#include <stddef.h>

class EventLoop;

class UdpDistribute {
    struct Recipient
        : boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::auto_unlink>> {

        UniqueSocketDescriptor fd;
        SocketEvent event;

        Recipient(EventLoop &_event_loop, UniqueSocketDescriptor &&_fd);
        ~Recipient();

        void RemoveAndDestroy() {
            delete this;
        }

    private:
        void EventCallback(unsigned) {
            RemoveAndDestroy();
        }
    };

    EventLoop &event_loop;

    boost::intrusive::list<Recipient,
                           boost::intrusive::constant_time_size<false>> recipients;

public:
    explicit UdpDistribute(EventLoop &_event_loop):event_loop(_event_loop) {}

    ~UdpDistribute() {
        Clear();
    }

    /**
     * Throws std::system_error on error.
     */
    UniqueSocketDescriptor Add();
    void Clear();

    void Packet(const void *payload, size_t payload_length);
};

#endif
