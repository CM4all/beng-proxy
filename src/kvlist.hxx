/*
 * List of key/value pairs.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_KVLIST_HXX
#define BENG_PROXY_KVLIST_HXX

#include <inline/compiler.h>

#include <boost/intrusive/slist.hpp>

class KeyValueList {
public:
    struct Item : boost::intrusive::slist_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>> {
        const char *key, *value;

        Item(const char *_key, const char *_value)
            :key(_key), value(_value) {}
    };

private:
    typedef boost::intrusive::slist<Item,
                                    boost::intrusive::constant_time_size<false>> List;

    typedef List::const_iterator const_iterator;

    List list;

public:
    KeyValueList() = default;
    KeyValueList(const KeyValueList &) = delete;
    KeyValueList(KeyValueList &&src) {
        list.swap(src.list);
    }

    template<typename Alloc>
    KeyValueList(Alloc &&alloc, const KeyValueList &src) {
        for (const auto &i : src)
            Add(alloc, alloc.DupString(i.key), alloc.DupString(i.value));
    }

    KeyValueList &operator=(KeyValueList &&src) {
        list.swap(src.list);
        return *this;
    }

    const_iterator begin() const {
        return list.begin();
    }

    const_iterator end() const {
        return list.end();
    }

    gcc_pure
    bool IsEmpty() const {
        return list.empty();
    }

    void Clear() {
        list.clear();
    }

    template<typename Alloc>
    void Add(Alloc &&alloc, const char *key, const char *value) {
        auto item = alloc.template New<Item>(key, value);
        list.push_front(*item);
    }

    void Reverse() {
        list.reverse();
    }
};

#endif
