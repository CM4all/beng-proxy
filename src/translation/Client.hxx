// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

class AllocatorPtr;
class EventLoop;
class Lease;
class CancellablePointer;
class SocketDescriptor;
class StopwatchPtr;
struct TranslateRequest;
class TranslateHandler;

/**
 * Call the translation server.
 */
void
translate(AllocatorPtr alloc, EventLoop &event_loop,
	  StopwatchPtr stopwatch,
	  SocketDescriptor fd, Lease &lease,
	  const TranslateRequest &request,
	  TranslateHandler &handler,
	  CancellablePointer &cancel_ptr) noexcept;
