/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef THREADED_STOCK_HXX
#define THREADED_STOCK_HXX

#include <forward_list>
#include <mutex>

/**
 * A container which manages equivalent reusable instances of a type,
 * e.g. database connections.
 */
template<typename T>
class ThreadedStock {
    typedef T Item;
    typedef std::forward_list<T> List;

    std::mutex mutex;

    /**
     * A list of idle items.
     */
    List items;

    class Lease {
        ThreadedStock<T> &stock;

        List items;

    public:
        Lease(ThreadedStock<T> &_stock, List &&_list)
            :stock(_stock), items(std::move(_list)) {}

        Lease(Lease &&src)
            :stock(src.stock) {
            items.swap(src.items);
        }

        ~Lease() {
            stock.Put(std::move(items));
        }

        T &operator*() {
            return items.front();
        }

        T *operator->() {
            return &items.front();
        }
    };

public:
    template<typename... Args>
    Lease Get(Args&&... args) {
        List tmp;

        {
            std::unique_lock<std::mutex> lock(mutex);
            if (!items.empty())
                tmp.splice_after(tmp.before_begin(), items, items.begin());
        }

        if (tmp.empty())
            tmp.emplace_front(std::forward<Args>(args)...);

        return Lease(*this, std::move(tmp));
    }

private:
    void Put(List &&src) {
        const std::unique_lock<std::mutex> lock(mutex);
        items.splice_after(items.before_begin(), std::move(src));
    }
};

#endif
