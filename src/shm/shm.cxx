/*
 * Shared memory for sharing data between worker processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "shm.hxx"
#include "system/Error.hxx"
#include "util/RefCount.hxx"

#include <inline/poison.h>
#include <inline/list.h>
#include <daemon/log.h>

#include <boost/interprocess/sync/interprocess_mutex.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>

#include <assert.h>
#include <stdint.h>
#include <sys/mman.h>
#include <errno.h>
#include <string.h>

struct shm {
    RefCount ref;

    const size_t page_size;
    const unsigned num_pages;

    /** this lock protects the linked list */
    boost::interprocess::interprocess_mutex mutex;

    struct Page {
        struct list_head siblings;

        unsigned num_pages;
        uint8_t *data;
    };

    struct list_head available;
    Page pages[1];

    shm(size_t _page_size, unsigned _num_pages)
        :page_size(_page_size), num_pages(_num_pages) {
        ref.Init();

        list_init(&available);
        list_add(&pages[0].siblings, &available);
        pages[0].num_pages = num_pages;
        pages[0].data = GetData();
    }

    static unsigned CalcHeaderPages(size_t page_size, unsigned num_pages) {
        size_t header_size = sizeof(struct shm) +
            (num_pages - 1) * sizeof(Page);
        return (header_size + page_size - 1) / page_size;
    }

    unsigned CalcHeaderPages() const {
        return CalcHeaderPages(page_size, num_pages);
    }

    uint8_t *At(size_t offset) {
        return (uint8_t *)this + offset;
    }

    const uint8_t *At(size_t offset) const {
        return (const uint8_t *)this + offset;
    }

    uint8_t *GetData() {
        return At(page_size * CalcHeaderPages());
    }

    const uint8_t *GetData() const {
        return At(page_size * CalcHeaderPages());
    }

    unsigned PageNumber(const void *p) const {
        ptrdiff_t difference = (const uint8_t *)p - GetData();
        unsigned page_number = difference / page_size;
        assert(difference % page_size == 0);

        assert(page_number < num_pages);

        return page_number;
    }

    Page *FindAvailable(unsigned want_pages);
    Page *SplitPage(Page *page, unsigned want_pages);

    /**
     * Merge this page with its adjacent pages if possible, to create
     * bigger "available" areas.
     */
    void Merge(Page *page);

    void *Allocate(unsigned want_pages);
    void Free(const void *p);
};

struct shm *
shm_new(size_t page_size, unsigned num_pages)
{
    assert(page_size >= sizeof(size_t));
    assert(num_pages > 0);

    const unsigned header_pages = shm::CalcHeaderPages(page_size, num_pages);
    void *p = mmap(nullptr, page_size * (header_pages + num_pages),
                   PROT_READ|PROT_WRITE,
                   MAP_ANONYMOUS|MAP_SHARED,
                   -1, 0);
    if (p == (void *)-1)
        throw MakeErrno("mmap() failed");

    return ::new(p) struct shm(page_size, num_pages);
}

void
shm_ref(struct shm *shm)
{
    assert(shm != nullptr);

    shm->ref.Get();
}

void
shm_close(struct shm *shm)
{
    unsigned header_pages;
    int ret;

    assert(shm != nullptr);

    if (shm->ref.Put())
        shm->~shm();

    header_pages = shm->CalcHeaderPages();
    ret = munmap(shm, shm->page_size * (header_pages + shm->num_pages));
    if (ret < 0)
        daemon_log(1, "munmap() failed: %s\n", strerror(errno));
}

size_t
shm_page_size(const struct shm *shm)
{
    return shm->page_size;
}

shm::Page *
shm::FindAvailable(unsigned want_pages)
{
    for (Page *page = (Page *)available.next;
         &page->siblings != &available;
         page = (Page *)page->siblings.next)
        if (page->num_pages >= want_pages)
            return page;

    return nullptr;
}

shm::Page *
shm::SplitPage(Page *page, unsigned want_pages)
{
    assert(page->num_pages > want_pages);

    page->num_pages -= want_pages;

    page[page->num_pages].data = page->data + page_size * page->num_pages;
    page += page->num_pages;
    page->num_pages = want_pages;

    return page;
}

void *
shm::Allocate(unsigned want_pages)
{
    assert(want_pages > 0);

    boost::interprocess::scoped_lock<boost::interprocess::interprocess_mutex> lock(mutex);

    Page *page = FindAvailable(want_pages);
    if (page == nullptr) {
        return nullptr;
    }

    assert(page->num_pages >= want_pages);

    page = (Page *)available.next;
    if (page->num_pages == want_pages) {
        list_remove(&page->siblings);

        lock.unlock();

        poison_undefined(page->data, page_size * want_pages);
        return page->data;
    } else {
        page = SplitPage(page, want_pages);

        lock.unlock();

        poison_undefined(page->data, page_size * want_pages);
        return page->data;
    }
}

void *
shm_alloc(struct shm *shm, unsigned want_pages)
{
    return shm->Allocate(want_pages);
}

/** merge this page with its adjacent pages if possible, to create
    bigger "available" areas */
void
shm::Merge(Page *page)
{
    unsigned page_number = PageNumber(page->data);

    /* merge with previous page? */

    Page *other = (Page *)page->siblings.prev;
    if (&other->siblings != &available &&
        PageNumber(other->data) + other->num_pages == page_number) {
        other->num_pages += page->num_pages;
        list_remove(&page->siblings);
        page = other;
    }

    /* merge with next page? */

    other = (Page *)page->siblings.next;
    if (&other->siblings != &available &&
        page_number + page->num_pages == PageNumber(other->data)) {
        page->num_pages += other->num_pages;
        list_remove(&other->siblings);
    }
}

void
shm::Free(const void *p)
{
    unsigned page_number = PageNumber(p);
    Page *page = &pages[page_number];
    Page *prev;

    poison_noaccess(page->data, page_size * page->num_pages);

    boost::interprocess::scoped_lock<boost::interprocess::interprocess_mutex> lock(mutex);

    for (prev = (Page *)&available;
         prev->siblings.next != &available;
         prev = (Page *)prev->siblings.next) {
    }

    list_add(&page->siblings, &prev->siblings);

    Merge(page);
}

void
shm_free(struct shm *shm, const void *p)
{
    shm->Free(p);
}
