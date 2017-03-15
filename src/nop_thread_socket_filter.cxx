/*
 * A thread_socket_filter implementation for debugging.  It performs a
 * no-op on all data.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "nop_thread_socket_filter.hxx"
#include "thread_socket_filter.hxx"
#include "pool.hxx"

#include <string.h>

class NopThreadSocketFilter final : public ThreadSocketFilterHandler {
public:
    /* virtual methods from class ThreadSocketFilterHandler */
    void Run(ThreadSocketFilter &f) override;
};

void
NopThreadSocketFilter::Run(ThreadSocketFilter &f)
{
    const std::lock_guard<std::mutex> lock(f.mutex);
    f.handshaking = false;
    f.decrypted_input.MoveFrom(f.encrypted_input);
    f.encrypted_output.MoveFrom(f.plain_output);
}

/*
 * constructor
 *
 */

ThreadSocketFilterHandler *
nop_thread_socket_filter_new()
{
    return new NopThreadSocketFilter();
}
