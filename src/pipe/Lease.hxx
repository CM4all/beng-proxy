// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "stock/PutAction.hxx"
#include "io/FileDescriptor.hxx"

#include <utility>

class PipeStock;
class StockItem;

class PipeLease {
	PipeStock *stock;
	StockItem *item = nullptr;
	FileDescriptor read_fd = FileDescriptor::Undefined();
	FileDescriptor write_fd = FileDescriptor::Undefined();

public:
	explicit PipeLease(PipeStock *_stock) noexcept
		:stock(_stock) {}

	PipeLease(PipeLease &&src) noexcept
		:stock(src.stock), item(std::exchange(src.item, nullptr)),
		 read_fd(std::exchange(src.read_fd, FileDescriptor::Undefined())),
		 write_fd(std::exchange(src.write_fd, FileDescriptor::Undefined())) {}

	PipeLease &operator=(PipeLease &&src) noexcept {
		using std::swap;
		swap(stock, src.stock);
		swap(item, src.item);
		swap(read_fd, src.read_fd);
		swap(write_fd, src.write_fd);
		return *this;
	}

	bool IsDefined() const noexcept {
		return read_fd.IsDefined();
	}

	/**
	 * Throws on error.
	 */
	void Create();

	/**
	 * Ensure that there is a pipe.
	 *
	 * Throws on error.
	 */
	void EnsureCreated() {
		if (!IsDefined())
			Create();
	}

	void Release(PutAction action) noexcept;

	void ReleaseIfStock() noexcept {
		if (item != nullptr)
			Release(PutAction::REUSE);
	}

	void CloseWriteIfNotStock() noexcept {
		if (item == nullptr && write_fd.IsDefined())
			write_fd.Close();
	}

	FileDescriptor GetReadFd() noexcept {
		return read_fd;
	}

	FileDescriptor GetWriteFd() noexcept {
		return write_fd;
	}
};
