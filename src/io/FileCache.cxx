// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "FileCache.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "util/AllocatedArray.hxx"
#include "util/SharedLease.hxx"

#include <cassert>
#include <string>

#include <fcntl.h> // for AT_EMPTY_PATH
#include <sys/inotify.h>
#include <sys/stat.h> // for struct statx

using std::string_view_literals::operator""sv;

struct FileCache::Item final
	: IntrusiveHashSetHook<>,
	  InotifyWatch,
	  SharedAnchor
{
	const std::string path;
	const std::size_t path_hash;

	const AllocatedArray<std::byte> contents;

	Item(InotifyManager &inotify_manager, StringWithHash key,
	     AllocatedArray<std::byte> &&_contents) noexcept
		:InotifyWatch(inotify_manager),
		 path(key.value), path_hash(key.hash),
		 contents(std::move(_contents))
	{
	}

	bool Enable() noexcept {
		return TryAddWatch(path.c_str(),
				   IN_ONESHOT|IN_DELETE_SELF|IN_MODIFY|IN_MOVE_SELF);
	}

	void Disable() noexcept {
		RemoveWatch();
	}

	bool IsDisabled() const noexcept {
		return !IsWatching();
	}

	bool IsUnused() const noexcept {
		return SharedAnchor::IsAbandoned();
	}

	// virtual methods from InotifyWatch
	void OnInotify([[maybe_unused]] unsigned mask,
		       [[maybe_unused]] const char *name) noexcept override {
		assert(!IsWatching()); // it's oneshot

		IntrusiveHashSetHook::unlink();
		Disable();

		if (IsUnused())
			/* unused, delete immediately */
			delete this;
	}

	/* virtual methods from SharedAnchor */
	void OnAbandoned() noexcept override {
		if (IsDisabled())
			delete this;
	}

	void OnBroken() noexcept override {
		assert(!IsAbandoned());

		if (!IsDisabled()) {
			IntrusiveHashSetHook::unlink();
			Disable();
		}
	}
};

inline StringWithHash
FileCache::ItemGetKey::operator()(const Item &item) const noexcept
{
	return StringWithHash{item.path, item.path_hash};
}

FileCache::FileCache(EventLoop &event_loop) noexcept
	:inotify_manager(event_loop)
{
}

FileCache::~FileCache() noexcept
{
	assert(map.empty());
}

void
FileCache::Flush() noexcept
{
	map.clear_and_dispose([](auto *item){
		item->Disable();
		if (item->IsUnused())
			delete item;
	});
}

void
FileCache::BeginShutdown() noexcept
{
	inotify_manager.BeginShutdown();

	Flush();
}

static AllocatedArray<std::byte>
LoadFile(const char *path, std::size_t max_size) noexcept
{
	UniqueFileDescriptor fd;
	if (!fd.OpenReadOnly(path))
		return nullptr;

	struct statx st;
	if (statx(fd.Get(), "", AT_EMPTY_PATH, STATX_TYPE|STATX_SIZE, &st) < 0)
		return nullptr;

	if (!S_ISREG(st.stx_mode) || std::cmp_greater(st.stx_size, max_size))
		return nullptr;

	AllocatedArray<std::byte> contents{static_cast<std::size_t>(st.stx_size)};

	ssize_t nbytes = fd.Read(contents);
	if (nbytes < 0 || static_cast<std::size_t>(nbytes) != contents.size())
		return nullptr;

	return contents;
}

std::pair<std::span<const std::byte>, SharedLease>
FileCache::Get(const char *path, std::size_t max_size) noexcept
{
	assert(path != nullptr);

	const StringWithHash key{path};

	auto [it, inserted] = map.insert_check(key);
	if (!inserted) {
		assert(!IsShuttingDown());

		auto &item = *it;
		assert(!item.IsDisabled());

		if (item.contents.size() > max_size) [[unlikely]]
			return {};

		return {item.contents, item};
	}

	auto contents = LoadFile(path, max_size);
	if (contents == nullptr)
		return {};

	auto *item = new Item(inotify_manager, key, std::move(contents));
	if (item->Enable())
		map.insert_commit(it, *item);

	return {item->contents, *item};
}
