/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef EXPANDABLE_STRING_LIST_HXX
#define EXPANDABLE_STRING_LIST_HXX

#include "translation/Features.hxx"
#include "util/ShallowCopy.hxx"

#include <inline/compiler.h>

#include <iterator>

class AllocatorPtr;
class MatchInfo;
template<typename T> struct ConstBuffer;

class ExpandableStringList final {
    struct Item {
        Item *next = nullptr;

        const char *value;

#if TRANSLATION_ENABLE_EXPAND
        bool expandable;

        Item(const char *_value, bool _expandable)
            :value(_value), expandable(_expandable) {}
#else
        Item(const char *_value, bool)
            :value(_value) {}
#endif
    };

    Item *head = nullptr;

public:
    ExpandableStringList() = default;
    ExpandableStringList(ExpandableStringList &&) = default;
    ExpandableStringList &operator=(ExpandableStringList &&src) = default;

    constexpr ExpandableStringList(ShallowCopy,
                                   const ExpandableStringList &src)
        :head(src.head) {}

    ExpandableStringList(AllocatorPtr alloc, const ExpandableStringList &src);

    gcc_pure
    bool IsEmpty() const {
        return head == nullptr;
    }

    class const_iterator final
        : public std::iterator<std::forward_iterator_tag, const char *> {

        const Item *cursor;

    public:
        const_iterator(const Item *_cursor):cursor(_cursor) {}

        bool operator!=(const const_iterator &other) const {
            return cursor != other.cursor;
        }

        const char *operator*() const {
            return cursor->value;
        }

        const_iterator &operator++() {
            cursor = cursor->next;
            return *this;
        }
    };

    const_iterator begin() const {
        return {head};
    }

    const_iterator end() const {
        return {nullptr};
    }

#if TRANSLATION_ENABLE_EXPAND
    gcc_pure
    bool IsExpandable() const;

    /**
     * Throws std::runtime_error on error.
     */
    void Expand(AllocatorPtr alloc, const MatchInfo &match_info);
#endif

    class Builder final {
        Item **tail_r;

        Item *last;

    public:
        Builder() = default;

        Builder(ExpandableStringList &_list)
            :tail_r(&_list.head), last(nullptr) {}

        /**
         * Add a new item to the end of the list.  The allocator is only
         * used to allocate the item structure, it does not copy the
         * string.
         */
        void Add(AllocatorPtr alloc, const char *value, bool expandable);

#if TRANSLATION_ENABLE_EXPAND
        bool CanSetExpand() const {
            return last != nullptr && !last->expandable;
        }

        void SetExpand(const char *value) const {
            last->value = value;
            last->expandable = true;
        }
#endif
    };

    ConstBuffer<const char *> ToArray(AllocatorPtr alloc) const;
};

#endif
