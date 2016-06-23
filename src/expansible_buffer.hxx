/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_EXPANSIBLE_BUFFER_HXX
#define BENG_EXPANSIBLE_BUFFER_HXX

#include <stddef.h>

struct pool;
struct StringView;

/**
 * A buffer which grows automatically.  Compared to growing_buffer, it
 * is optimized to be read as one complete buffer, instead of many
 * smaller chunks.  Additionally, it can be reused.
 */
class ExpansibleBuffer {
    struct pool &pool;
    char *buffer;
    const size_t hard_limit;
    size_t max_size;
    size_t size = 0;

public:
    /**
     * @param _hard_limit the buffer will refuse to grow beyond this size
     */
    ExpansibleBuffer(struct pool &_pool,
                     size_t initial_size, size_t _hard_limit);

    ExpansibleBuffer(const ExpansibleBuffer &) = delete;
    ExpansibleBuffer &operator=(const ExpansibleBuffer &) = delete;

    bool IsEmpty() const {
        return size == 0;
    }

    size_t GetSize() const {
        return size;
    }

    void Clear();

    /**
     * @return nullptr if the operation would exceed the hard limit
     */
    void *Write(size_t length);

    /**
     * @return false if the operation would exceed the hard limit
     */
    bool Write(const void *p, size_t length);

    /**
     * @return false if the operation would exceed the hard limit
     */
    bool Write(const char *p);

    /**
     * @return false if the operation would exceed the hard limit
     */
    bool Set(const void *p, size_t new_size);

    bool Set(StringView p);

    const void *Read(size_t *size_r) const;

    const char *ReadString();

    StringView ReadStringView() const;

    void *Dup(struct pool &_pool) const;

    char *StringDup(struct pool &_pool) const;

private:
    bool Resize(size_t new_max_size);
};

#endif
