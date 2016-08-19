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

#ifndef NET_ADDRESS_INFO_HXX
#define NET_ADDRESS_INFO_HXX

#include "SocketAddress.hxx"

#include <algorithm>

#include <netdb.h>

class AllocatedSocketAddress;
struct addrinfo;

class AddressInfo {
	struct addrinfo *value = nullptr;

public:
	AddressInfo() = default;
	explicit AddressInfo(struct addrinfo *_value):value(_value) {}

	AddressInfo(AddressInfo &&src):value(src.value) {
		src.value = nullptr;
	}

	~AddressInfo() {
		freeaddrinfo(value);
	}

	AddressInfo &operator=(AddressInfo &&src) {
		std::swap(value, src.value);
		return *this;
	}

	bool empty() const {
		return value == nullptr;
	}

	SocketAddress front() const {
		return {value->ai_addr, value->ai_addrlen};
	}

	const struct addrinfo *operator->() const {
		return value;
	}

	class const_iterator {
		struct addrinfo *cursor;

	public:
		explicit constexpr const_iterator(struct addrinfo *_cursor)
			:cursor(_cursor) {}

		constexpr bool operator==(const_iterator other) const {
			return cursor == other.cursor;
		}

		constexpr bool operator!=(const_iterator other) const {
			return cursor != other.cursor;
		}

		const_iterator &operator++() {
			cursor = cursor->ai_next;
			return *this;
		}

		SocketAddress operator*() const {
			return {cursor->ai_addr, cursor->ai_addrlen};
		}

		const struct addrinfo *operator->() const {
			return cursor;
		}
	};

	const_iterator begin() const {
		return const_iterator(value);
	}

	const_iterator end() const {
		return const_iterator(nullptr);
	}
};

#endif
