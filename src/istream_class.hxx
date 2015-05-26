/*
 * Asynchronous input stream API.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef ISTREAM_CLASS_HXX
#define ISTREAM_CLASS_HXX

#include <sys/types.h>

struct istream;

struct istream_class {
    /**
     * How much data is available? 
     *
     * @param partial if false, the stream must provide the data size
     * until the end of the stream; for partial, a minimum estimate is
     * ok
     * @return the number of bytes available or -1 if the object does
     * not know
     */
    off_t (*available)(struct istream *istream, bool partial);

    /**
     * Skip data without processing it.  By skipping 0 bytes, you can
     * test whether the stream is able to skip at all.
     *
     * @return the number of bytes skipped or -1 if skipping is not supported
     */
    off_t (*skip)(struct istream *istream, off_t length);

    /**
     * Try to read from the stream.  If the stream can read data
     * without blocking, it must provide data.  It may invoke the
     * callbacks any number of times, supposed that the handler itself
     * doesn't block.
     *
     * If the stream does not provide data immediately (and it is not
     * at EOF yet), it must install an event and invoke the handler
     * later, whenever data becomes available.
     *
     * Whenever the handler reports it is blocking, the responsibility
     * for calling back (and calling this function) is handed back to
     * the istream handler.
     */
    void (*read)(struct istream *istream);

    /**
     * Close the istream object, and return the remaining data as a
     * file descriptor.  This fd can be read until end-of-stream.
     * Returns -1 if this is not possible (the istream object is still
     * usable).
     */
    int (*as_fd)(struct istream *istream);

    /**
     * Close the stream and free resources.  This must not be called
     * after the handler's eof() / abort() callbacks were invoked.
     */
    void (*close)(struct istream *istream);
};

#endif
