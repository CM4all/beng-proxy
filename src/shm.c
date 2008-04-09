/*
 * Shared memory for sharing data between worker processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "shm.h"

#include <inline/poison.h>
#include <inline/list.h>
#include <daemon/log.h>

#include <assert.h>
#include <sys/mman.h>
#include <errno.h>

struct page {
    struct list_head siblings;

    unsigned num_pages;
    unsigned char *data;
};

struct shm {
    size_t page_size;
    unsigned num_pages;

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
             PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_SHARED, -1, 0);
    if (p == NULL)
        return NULL;

    shm = (struct shm *)p;
    shm->page_size = page_size;
    shm->num_pages = num_pages;

    list_init(&shm->available);
    list_add(&shm->pages[0].siblings, &shm->available);
    shm->pages[0].num_pages = num_pages;
    shm->pages[0].data = shm_data(shm);

    poison_noaccess(shm_data(shm), page_size * num_pages);

    return shm;
}

void
shm_close(struct shm *shm)
{
    unsigned header_pages;
    int ret;

    assert(shm != NULL);

    header_pages = calc_header_pages(shm->page_size, shm->num_pages);
    ret = munmap(shm, shm->page_size * (header_pages + shm->num_pages));
    if (ret < 0)
        daemon_log(1, "munmap() failed: %s\n", strerror(errno));
}

void *
shm_alloc(struct shm *shm)
{
    struct page *page;

    /* XXX synchronize */

    if (list_empty(&shm->available))
        return NULL;

    page = (struct page *)shm->available.next;
    if (page->num_pages == 1) {
        list_remove(&page->siblings);
        return page->data;
    } else {
        --page->num_pages;
        page[page->num_pages].data = page->data + shm->page_size * page->num_pages;
        page += page->num_pages;
        page->num_pages = 1;
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

void
shm_free(struct shm *shm, const void *p)
{
    unsigned page_number = shm_page_number(shm, p);
    struct page *page = &shm->pages[page_number];

    poison_noaccess(page->data, shm->page_size * page->num_pages);

    /* XXX synchronize */

    list_add(&page->siblings, &shm->available);
    /* XXX sort; merge adjacent pages */
}
