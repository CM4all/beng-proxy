// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "stopwatch.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "util/LeakDetector.hxx"
#include "util/SpanCast.hxx"
#include "util/StaticVector.hxx"
#include "util/StringBuilder.hxx"

#include <fmt/core.h>

#include <chrono>
#include <list>
#include <string>

#include <assert.h>
#include <stdio.h>
#include <string.h>

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

static constexpr auto
ToMs(std::chrono::steady_clock::duration d) noexcept
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(d).count();
}

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

	auto w = b.Write();
	char *p = fmt::format_to_n(w.data(), w.size(), " init={}ms", ToMs(time - root_time)).out;
	b.Extend(p - w.data());

	for (const auto &i : events) {
		w = b.Write();
		p = fmt::format_to_n(w.data(), w.size(), " {}={}ms", i.name, ToMs(i.time - root_time)).out;
		b.Extend(p - w.data());
	}

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
