// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "util/BindMethod.hxx"

#include <exception>

struct pool;
class UnusedIstreamPtr;
class NullSink;

/**
 * An istream handler which silently discards everything and ignores errors.
 *
 * @param callback an optional function that will be invoked when the
 * #Istream ends (or fails)
 */
NullSink &
NewNullSink(struct pool &p, UnusedIstreamPtr istream,
	    BoundMethod<void(std::exception_ptr &&error) noexcept> callback=nullptr) noexcept;

void
ReadNullSink(NullSink &sink) noexcept;
