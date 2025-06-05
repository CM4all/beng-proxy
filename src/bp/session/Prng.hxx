// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <random>

/**
 * The random engine used to generate session ids.
 */
using SessionPrng = std::mt19937_64;
