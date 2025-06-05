// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <cstddef>

typedef struct bio_st BIO;
template<typename T> class ForeignFifoBuffer;

/**
 * Create an OpenSSL BIO wrapper for a #ForeignFifoBuffer.
 */
BIO *
NewFifoBufferBio(ForeignFifoBuffer<std::byte> &buffer) noexcept;

/**
 * Global deinitialization.
 */
void
DeinitFifoBufferBio() noexcept;
