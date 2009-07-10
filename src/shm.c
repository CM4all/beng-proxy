/*
 * Shared memory for sharing data between worker processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "shm.h"
#include "lock.h"
#include "refcount.h"

#include <inline/poison.h>
#include <inline/list.h>
#include <daemon/log.h>

#include <assert.h>
#include <sys/mman.h>
#include <errno.h>
#include <string.h>

struct page {
    struct list_head siblings;

    unsigned num_pages;
    unsigned char *data;
};

struct shm {
    struct refcount ref;

    size_t page_size;
    unsigned num_pages;

    /** this lock protects the linked list */
    struct lock lock;

    struct list_head available;
    struct page pages[1];
};

static inline unsigned
calc_header_pages(size_t page_size, unsigned num_pages)
{
    size_t header_size;

    header_size = sizeof(struct shm) +
        (num_pages - 1) * sizeof(struct page);
    return (header_size + page_size - 1) / page_size;
}

static unsigned char *
shm_data(struct shm *shm)
{
    unsigned header_pages = calc_header_pages(shm->page_size, shm->num_pages);
    unsigned char *base = (unsigned char*)shm;

    return base + shm->page_size * header_pages;
}

struct shm *
shm_new(size_t page_size, unsigned num_pages)
{
    struct shm *shm;
    unsigned header_pages;
    unsigned char *p;

    assert(page_size >= sizeof(size_t));
    assert(num_pages > 0);

    header_pages = calc_header_pages(page_size, num_pages);
    p = mmap(NULL, page_size * (header_pages + num_pages),
             PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_SHARED|MAP_NORESERVE,
             -1, 0);
    if (p == NULL)
        return NULL;

    shm = (struct shm *)p;
    refcount_init(&shm->ref);
    shm->page_size = page_size;
    shm->num_pages = num_pages;

    lock_init(&shm->lock);

    list_init(&shm->available);
    list_add(&shm->pages[0].siblings, &shm->available);
    shm->pages[0].num_pages = num_pages;
    shm->pages[0].data = shm_data(shm);

    poison_noaccess(shm_data(shm), page_size * num_pages);

    return shm;
}

void
shm_ref(struct shm *shm)
{
    assert(shm != NULL);

    refcount_get(&shm->ref);
}

void
shm_close(struct shm *shm)
{
    unsigned header_pages;
    int ret;

    assert(shm != NULL);

    if (refcount_put(&shm->ref))
        lock_destroy(&shm->lock);

    header_pages = calc_header_pages(shm->page_size, shm->num_pages);
    ret = munmap(shm, shm->page_size * (header_pages + shm->num_pages));
    if (ret < 0)
        daemon_log(1, "munmap() failed: %s\n", strerror(errno));
}

size_t
shm_page_size(const struct shm *shm)
{
    return shm->page_size;
}

static struct page *
shm_find_available(struct shm *shm, unsigned num_pages)
{
    struct page *page;

    for (page = (struct page *)shm->available.next;
         &page->siblings != &shm->available;
         page = (struct page *)page->siblings.next)
        if (page->num_pages >= num_pages)
            return page;

    return NULL;
}

static struct page *
shm_split_page(const struct shm *shm, struct page *page, unsigned num_pages)
{
    assert(page->num_pages > num_pages);

    page->num_pages -= num_pages;

    page[page->num_pages].data = page->data + shm->page_size * page->num_pages;
    page += page->num_pages;
    page->num_pages = num_pages;

    return page;
}

void *
shm_alloc(struct shm *shm, unsigned num_pages)
{
    struct page *page;

    assert(num_pages > 0);

    lock_lock(&shm->lock);

    page = shm_find_available(shm, num_pages);
    if (page == NULL) {
        lock_unlock(&shm->lock);
        return NULL;
    }

    assert(page->num_pages >= num_pages);

    page = (struct page *)shm->available.next;
    if (page->num_pages == num_pages) {
        list_remove(&page->siblings);
        lock_unlock(&shm->lock);

        poison_undefined(page->data, shm->page_size * num_pages);
        return page->data;
    } else {
        page = shm_split_page(shm, page, num_pages);

        lock_unlock(&shm->lock);

        poison_undefined(page->data, shm->page_size * num_pages);
        return page->data;
    }
}

static unsigned
shm_page_number(struct shm *shm, const void *p)
{
    ptrdiff_t difference = (const unsigned char*)p - shm_data(shm);
    unsigned page_number = difference / shm->page_size;
    assert(difference % shm->page_size == 0);

    assert(page_number < shm->num_pages);

    return page_number;
}

/** merge this page with its adjacent pages if possible, to create
    bigger "available" areas */
static void
shm_merge(struct shm *shm, struct page *page)
{
    unsigned page_number = shm_page_number(shm, page->data);
    struct page *other;

    /* merge with previous page? */

    other = (struct page *)page->siblings.prev;
    if (&other->siblings != &shm->available &&
        shm_page_number(shm, other->data) + other->num_pages == page_number) {
        other->num_pages += page->num_pages;
        list_remove(&page->siblings);
        page = other;
    }

    /* merge with next page? */

    other = (struct page *)page->siblings.next;
    if (&other->siblings != &shm->available &&
        page_number + page->num_pages == shm_page_number(shm, other->data)) {
        page->num_pages += other->num_pages;
        list_remove(&other->siblings);
    }
}

void
shm_free(struct shm *shm, const void *p)
{
    unsigned page_number = shm_page_number(shm, p);
    struct page *page = &shm->pages[page_number];
    struct page *prev;

    poison_noaccess(page->data, shm->page_size * page->num_pages);

    lock_lock(&shm->lock);

    for (prev = (struct page *)&shm->available;
         prev->siblings.next != &shm->available;
         prev = (struct page *)prev->siblings.next) {
    }

    list_add(&page->siblings, &prev->siblings);

    shm_merge(shm, page);

    lock_unlock(&shm->lock);
}
