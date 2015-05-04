/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_STRSET_HXX
#define BENG_PROXY_STRSET_HXX

#include <inline/compiler.h>

#include <stddef.h>

struct pool;

/**
 * An unordered set of strings.
 */
struct strset {
    struct Item {
        Item *next;

        const char *value;
    };

    Item *head;

    void Init() {
        head = nullptr;
    }

    gcc_pure
    bool IsEmpty() const {
        return head == nullptr;
    }

    gcc_pure
    bool Contains(const char *p) const;

    /**
     * Add a string to the set.  It does not check whether the string
     * already exists.
     *
     * @param p the string value which must be allocated by the caller
     * @param pool a pool that is used to allocate the node (not the value)
     */
    void Add(struct pool &pool, const char *p);

    /**
     * Copy all strings from one set to this, creating duplicates of
     * all values from the specified pool.
     */
    void CopyFrom(struct pool &pool, const struct strset &s);
};

#define strset_for_each_item(item, s) \
    for (const strset::Item *item = (s)->head; item != nullptr; item = item->next)

#endif
