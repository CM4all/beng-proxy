// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "stopwatch.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "util/LeakDetector.hxx"
#include "util/SpanCast.hxx"
#include "util/StaticVector.hxx"
#include "util/StringBuilder.hxx"

#include <chrono>
#include <list>
#include <string>

#include <assert.h>
#include <stdio.h>
#include <string.h>

#if 0
#include <sys/resource.h>
#endif

static UniqueFileDescriptor stopwatch_fd;

struct StopwatchEvent {
	std::string name;

	std::chrono::steady_clock::time_point time =
		std::chrono::steady_clock::now();

	template<typename N>
	explicit StopwatchEvent(N &&_name) noexcept
		:name(std::forward<N>(_name)) {}
};

class Stopwatch final : LeakDetector
{
	const std::string name;

	const std::chrono::steady_clock::time_point time =
		std::chrono::steady_clock::now();

	std::list<std::shared_ptr<Stopwatch>> children;

	StaticVector<StopwatchEvent, 16> events;

#if 0
	/**
	 * Our own resource usage, measured when the stopwatch was
	 * started.
	 */
	struct rusage self;
#endif

	const bool dump;

public:
	template<typename N>
	Stopwatch(N &&_name, bool _dump) noexcept
		:name(std::forward<N>(_name)),
		 dump(_dump)
	{
#if 0
		getrusage(RUSAGE_SELF, &self);
#endif
	}

	~Stopwatch() noexcept {
		if (dump)
			Dump(time, 0);
	}

	template<typename C>
	void AddChild(C &&child) noexcept {
		children.emplace_back(std::forward<C>(child));
	}

	void RecordEvent(std::string_view name) noexcept;

	void Dump(std::chrono::steady_clock::time_point root_time,
		  size_t indent) noexcept;
};

void
stopwatch_enable(UniqueFileDescriptor fd) noexcept
{
	assert(fd.IsDefined());

	stopwatch_fd = std::move(fd);
}

bool
stopwatch_is_enabled() noexcept
{
	return stopwatch_fd.IsDefined();
}

static std::string
MakeStopwatchName(std::string name, const char *suffix) noexcept
{
	if (suffix != nullptr)
		name += suffix;

	constexpr size_t MAX_NAME = 96;
	if (name.length() > MAX_NAME)
		name.resize(MAX_NAME);

	return name;
}

static std::shared_ptr<Stopwatch>
stopwatch_new(std::string_view name, const char *suffix) noexcept
{
	if (!stopwatch_is_enabled())
		return nullptr;

	return std::make_shared<Stopwatch>(MakeStopwatchName(std::string{name}, suffix), true);
}

StopwatchPtr::StopwatchPtr(std::string_view name, const char *suffix) noexcept
	:stopwatch(stopwatch_new(name, suffix)) {}

StopwatchPtr::StopwatchPtr(Stopwatch *parent, std::string_view name,
			   const char *suffix) noexcept
{
	if (parent != nullptr) {
		stopwatch = std::make_shared<Stopwatch>(MakeStopwatchName(std::string{name}, suffix),
							false);
		parent->AddChild(stopwatch);
	}
}

StopwatchPtr::~StopwatchPtr() noexcept = default;

inline void
Stopwatch::RecordEvent(std::string_view event_name) noexcept
{
	if (events.full())
		/* array is full, do not record any more events */
		return;

	events.emplace_back(event_name);
}

void
StopwatchPtr::RecordEvent(std::string_view name) const noexcept
{
	if (stopwatch != nullptr)
		stopwatch->RecordEvent(name);
}

static constexpr long
ToLongMs(std::chrono::steady_clock::duration d) noexcept
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(d).count();
}

#if 0

static long
timeval_diff_ms(const struct timeval *a, const struct timeval *b) noexcept
{
	return (a->tv_sec - b->tv_sec) * 1000 +
		(a->tv_usec - b->tv_usec) / 1000;
}

#endif

inline void
Stopwatch::Dump(std::chrono::steady_clock::time_point root_time,
		size_t indent) noexcept
try {
	if (!stopwatch_fd.IsDefined())
		return;

	char message[1024];
	StringBuilder b(message);

	b.CheckAppend(indent);
	std::fill_n(b.GetTail(), indent, ' ');
	b.Extend(indent);

	b.Append(name.c_str());

	b.Format(" init=%ldms",
		 ToLongMs(time - root_time));

	for (const auto &i : events)
		b.Format(" %s=%ldms",
			 i.name.c_str(),
			 ToLongMs(i.time - time));

#if 0
	struct rusage new_self;
	getrusage(RUSAGE_SELF, &new_self);
	b.Format(" (beng-proxy=%ld+%ldms)",
		 timeval_diff_ms(&new_self.ru_utime, &self.ru_utime),
		 timeval_diff_ms(&new_self.ru_stime, &self.ru_stime));
#endif

	b.Append('\n');

	if (stopwatch_fd.Write(AsBytes(std::string_view{message})) < 0) {
		stopwatch_fd.Close();
		return;
	}

	indent += 2;

	for (const auto &child : children)
		child->Dump(root_time, indent);
} catch (StringBuilder::Overflow) {
}
