/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_STRSET_HXX
#define BENG_PROXY_STRSET_HXX

#include <inline/compiler.h>

#include <boost/intrusive/slist.hpp>

#include <stddef.h>

struct pool;

/**
 * An unordered set of strings.
 */
struct StringSet {
    struct Item : boost::intrusive::slist_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>> {
        const char *value;
    };

    typedef boost::intrusive::slist<Item,
                                    boost::intrusive::constant_time_size<false>> List;

    List list;

    StringSet() = default;
    StringSet(const StringSet &) = delete;
    StringSet(StringSet &&) = default;
    StringSet &operator=(const StringSet &) = delete;
    StringSet &operator=(StringSet &&src) = default;

    void Init() {
        list.clear();
    }

    gcc_pure
    bool IsEmpty() const {
        return list.empty();
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
    void CopyFrom(struct pool &pool, const StringSet &s);

    class const_iterator {
        List::const_iterator i;

    public:
        const_iterator(List::const_iterator _i):i(_i) {}

        bool operator!=(const const_iterator &other) const {
            return i != other.i;
        }

        const char *operator*() const {
            return i->value;
        }

        const_iterator &operator++() {
            ++i;
            return *this;
        }
    };

    const_iterator begin() const {
        return list.begin();
    }

    const_iterator end() const {
        return list.end();
    }
};

#endif
