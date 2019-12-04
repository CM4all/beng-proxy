/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "shm.hxx"
#include "system/Error.hxx"
#include "io/Logger.hxx"
#include "util/RefCount.hxx"
#include "util/Poison.hxx"

#include <boost/interprocess/sync/interprocess_mutex.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>
#include <boost/intrusive/set.hpp>

#include <assert.h>
#include <stdint.h>
#include <sys/mman.h>
#include <errno.h>
#include <string.h>

struct shm {
	RefCount ref;

	const size_t page_size;
	const unsigned num_pages;

	/**
	 * The data section of the first page.
	 */
	uint8_t *const data;

	/** this lock protects the linked list */
	boost::interprocess::interprocess_mutex mutex;

	struct Page
		: boost::intrusive::set_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>> {
		unsigned num_pages;

		bool operator<(const Page &other) const {
			return this < &other;
		}
	};

	typedef boost::intrusive::set<Page,
				      boost::intrusive::constant_time_size<false>> PageList;

	PageList available;
	Page pages[1];

	shm(size_t _page_size, unsigned _num_pages)
		:page_size(_page_size), num_pages(_num_pages),
		 data(At(page_size * CalcHeaderPages())) {
		pages[0].num_pages = num_pages;
		available.push_front(pages[0]);
	}

	static unsigned CalcHeaderPages(size_t page_size, unsigned num_pages) {
		size_t header_size = sizeof(struct shm) +
			(num_pages - 1) * sizeof(Page);
		return (header_size + page_size - 1) / page_size;
	}

	static size_t CalcMemoryMapSize(size_t page_size, unsigned num_pages) {
		return page_size * (CalcHeaderPages(page_size, num_pages) + num_pages);
	}

	unsigned CalcHeaderPages() const {
		return CalcHeaderPages(page_size, num_pages);
	}

	size_t CalcMemoryMapSize() const {
		return CalcMemoryMapSize(page_size, num_pages);
	}

	uint8_t *At(size_t offset) {
		return (uint8_t *)this + offset;
	}

	const uint8_t *At(size_t offset) const {
		return (const uint8_t *)this + offset;
	}

	uint8_t *GetData() {
		return data;
	}

	const uint8_t *GetData() const {
		return data;
	}

	uint8_t *PageData(unsigned page_number) {
		return GetData() + page_number * page_size;
	}

	const uint8_t *PageData(unsigned page_number) const {
		return GetData() + page_number * page_size;
	}

	uint8_t *PageData(Page &page) {
		return PageData(PageNumber(page));
	}

	const uint8_t *PageData(const Page &page) const {
		return PageData(PageNumber(page));
	}

	unsigned PageNumber(const Page &page) const {
		return &page - &pages[0];
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

	const size_t size = shm::CalcMemoryMapSize(page_size, num_pages);
	void *p = mmap(nullptr, size,
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
	assert(shm != nullptr);

	const size_t size = shm->CalcMemoryMapSize();

	if (shm->ref.Put())
		shm->~shm();

	if (munmap(shm, size) < 0)
		LogConcat(1, "shm", "munmap() failed: ", strerror(errno));
}

size_t
shm_page_size(const struct shm *shm)
{
	return shm->page_size;
}

shm::Page *
shm::FindAvailable(unsigned want_pages)
{
	for (auto &page : available)
		if (page.num_pages >= want_pages)
			return &page;

	return nullptr;
}

shm::Page *
shm::SplitPage(Page *page, unsigned want_pages)
{
	assert(page->num_pages > want_pages);

	page->num_pages -= want_pages;

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
	if (page == nullptr)
		return nullptr;

	assert(available.iterator_to(*page) == available.begin() ||
	       &*std::prev(available.iterator_to(*page)) < page);
	assert(std::next(available.iterator_to(*page)) == available.end() ||
	       &*std::next(available.iterator_to(*page)) > page);

	assert(page->num_pages >= want_pages);

	if (page->num_pages == want_pages) {
		available.erase(available.iterator_to(*page));

		lock.unlock();

		void *page_data = PageData(*page);
		PoisonUndefined(page_data, page_size * want_pages);
		return page_data;
	} else {
		page = SplitPage(page, want_pages);

		lock.unlock();

		void *page_data = PageData(*page);
		PoisonUndefined(page_data, page_size * want_pages);
		return page_data;
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
	/* merge with previous page? */

	assert(available.iterator_to(*page) == available.begin() ||
	       &*std::prev(available.iterator_to(*page)) < page);

	auto i = available.iterator_to(*page);

	if (i != available.begin()) {
		Page &previous = *std::prev(i);
		if (&previous + previous.num_pages == page) {
			previous.num_pages += page->num_pages;
			available.erase(i);

			page = &previous;
			i = available.iterator_to(*page);
		}
	}

	/* merge with next page? */

	assert(std::next(available.iterator_to(*page)) == available.end() ||
	       &*std::next(available.iterator_to(*page)) > page);

	if (i != std::prev(available.end())) {
		Page &next = *std::next(i);

		if (page + page->num_pages == &next) {
			page->num_pages += next.num_pages;
			available.erase(available.iterator_to(next));
		}
	}
}

void
shm::Free(const void *p)
{
	unsigned page_number = PageNumber(p);
	Page *page = &pages[page_number];

	PoisonInaccessible(PageData(*page), page_size * page->num_pages);

	boost::interprocess::scoped_lock<boost::interprocess::interprocess_mutex> lock(mutex);

	available.insert(*page);

	Merge(page);
}

void
shm_free(struct shm *shm, const void *p)
{
	shm->Free(p);
}
