/*
 * A thread_socket_filter implementation for debugging.  It performs a
 * no-op on all data.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "nop_thread_socket_filter.hxx"
#include "thread_socket_filter.hxx"
#include "pool.hxx"
#include "util/ForeignFifoBuffer.hxx"

#include <string.h>

class NopThreadSocketFilter final : public ThreadSocketFilterHandler {
public:
    /* virtual methods from class ThreadSocketFilterHandler */
    bool Run(ThreadSocketFilter &f, GError **error_r) override;

    void Destroy(ThreadSocketFilter &) override {
        delete this;
    }
};

bool
NopThreadSocketFilter::Run(ThreadSocketFilter &f, gcc_unused GError **error_r)
{
    const std::lock_guard<std::mutex> lock(f.mutex);
    f.decrypted_input.MoveFrom(f.encrypted_input);
    f.encrypted_output.MoveFrom(f.plain_output);
    return true;
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
