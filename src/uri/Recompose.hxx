// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

class AllocatorPtr;
struct DissectedUri;

char *
RecomposeUri(AllocatorPtr alloc, const DissectedUri &uri) noexcept;
