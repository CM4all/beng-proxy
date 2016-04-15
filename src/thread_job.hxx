/*
 * A job that shall be executed in a worker thread.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_THREAD_JOB_HXX
#define BENG_PROXY_THREAD_JOB_HXX

#include <boost/intrusive/list.hpp>

class ThreadJob
    : public boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>> {
public:
    enum class State {
        /**
         * The job is not in any queue.
         */
        INITIAL,

        /**
         * The job has been added to the queue, but is not being worked on
         * yet.
         */
        WAITING,

        /**
         * The job is being performed via run().
         */
        BUSY,

        /**
         * The job has finished, but the done() method has not been
         * invoked yet.
         */
        DONE,
    };

    State state = State::INITIAL;

    /**
     * Shall this job be enqueued again instead of invoking its done()
     * method?
     */
    bool again = false;

    /**
     * Is this job currently idle, i.e. not being worked on by a
     * worker thread?  This method may be called only from the main
     * thread.  A "true" return value guarantees that no worker thread
     * is and will be working on it, and its internal data structures
     * may be accessed without mutex protection.  Use this method with
     * caution.
     */
    bool IsIdle() const {
        return state == State::INITIAL;
    }

    virtual void Run() = 0;
    virtual void Done() = 0;
};

#endif
