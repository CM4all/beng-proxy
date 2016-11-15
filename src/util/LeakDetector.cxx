/*
 * Copyright (C) 2016 Max Kellermann <max@duempel.org>
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

#include "LeakDetector.hxx"

#ifndef NDEBUG

#include <mutex>

class LeakDetectorContainer {
	std::mutex mutex;

	boost::intrusive::list<LeakDetector,
			       boost::intrusive::constant_time_size<true>> list;

public:
	~LeakDetectorContainer() {
		assert(list.empty());
	}

	void Add(LeakDetector &l) {
		const std::unique_lock<std::mutex> lock(mutex);

		assert((list.size() == 0) == list.empty());
		const auto old_size = list.size();

		list.push_back(l);

		assert(!list.empty());
		const auto new_size = list.size();
		assert(new_size == old_size + 1);
	}

	void Remove(LeakDetector &l) {
		const std::unique_lock<std::mutex> lock(mutex);

		assert(!list.empty());
		assert(list.size() > 0);
		const auto old_size = list.size();

		list.erase(list.iterator_to(l));

		const auto new_size = list.size();
		assert(new_size + 1 == old_size);
		assert((new_size == 0) == list.empty());
	}
};

static LeakDetectorContainer leak_detector_container;

LeakDetector::LeakDetector()
{
	leak_detector_container.Add(*this);
	state = State::REGISTERED;
}

LeakDetector::~LeakDetector()
{
	assert(state == State::REGISTERED);

	leak_detector_container.Remove(*this);
	state = State::DESTRUCTED;
}

#endif
