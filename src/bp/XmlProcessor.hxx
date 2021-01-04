/*
 * Copyright 2007-2021 CM4all GmbH
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
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

#pragma once

#include "util/Compiler.h"

/** options for processor_new() */
enum processor_options {
	/** rewrite URLs */
	PROCESSOR_REWRITE_URL = 0x1,

	/** add prefix to marked CSS class names */
	PROCESSOR_PREFIX_CSS_CLASS = 0x2,

	/**
	 * Default URI rewrite mode is base=widget mode=focus.
	 */
	PROCESSOR_FOCUS_WIDGET = 0x4,

	/** add prefix to marked XML ids */
	PROCESSOR_PREFIX_XML_ID = 0x8,

	/** enable the c:embed element */
	PROCESSOR_CONTAINER = 0x10,

	/**
	 * Invoke the CSS processor for "style" element contents?
	 */
	PROCESSOR_STYLE = 0x20,

	/**
	 * Allow this widget to embed more instances of its own class.
	 */
	PROCESSOR_SELF_CONTAINER = 0x40,
};

struct pool;
template<typename T> class SharedPoolPtr;
struct WidgetContext;
class StopwatchPtr;
class UnusedIstreamPtr;
class Widget;
class StringMap;

gcc_pure
bool
processable(const StringMap &headers);

/**
 * Process the specified istream, and return the processed stream.
 *
 * @param widget the widget that represents the template
 */
UnusedIstreamPtr
processor_process(struct pool &pool,
		  const StopwatchPtr &parent_stopwatch,
		  UnusedIstreamPtr istream,
		  Widget &widget,
		  SharedPoolPtr<WidgetContext> ctx,
		  unsigned options);
