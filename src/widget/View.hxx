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

#include "ResourceAddress.hxx"
#include "bp/ForwardHeaders.hxx"
#include "util/Compiler.h"
#include "util/IntrusiveForwardList.hxx"

struct Transformation;
class AllocatorPtr;

struct WidgetView {
	WidgetView *next = nullptr;

	/**
	 * The name of this view; always NULL for the first (default)
	 * view.
	 */
	const char *name = nullptr;

	/** the base URI of this widget, as specified in the template */
	ResourceAddress address{nullptr};

	/**
	 * Filter client error messages?
	 */
	bool filter_4xx = false;

	/**
	 * Copy of #TranslateResponse::subst_alt_syntax from
	 * #TranslationCommand::SUBST_ALT_SYNTAX.
	 */
	bool subst_alt_syntax = false;

	/**
	 * Was the address inherited from another view?
	 */
	bool inherited = false;

	IntrusiveForwardList<Transformation> transformations;

	/**
	 * Which request headers are forwarded?
	 */
	HeaderForwardSettings request_header_forward = HeaderForwardSettings::MakeDefaultRequest();

	/**
	 * Which response headers are forwarded?
	 */
	HeaderForwardSettings response_header_forward = HeaderForwardSettings::MakeDefaultResponse();

	explicit WidgetView(const char *_name) noexcept
		:name(_name) {}

	explicit constexpr WidgetView(const ResourceAddress &_address) noexcept
		:address(ShallowCopy(), _address) {}

	void CopyFrom(AllocatorPtr alloc, const WidgetView &src) noexcept;

	WidgetView *Clone(AllocatorPtr alloc) const noexcept;

	void CopyChainFrom(AllocatorPtr alloc, const WidgetView &src) noexcept;
	WidgetView *CloneChain(AllocatorPtr alloc) const noexcept;

	/**
	 * Copy the specified address into the view, if it does not have an
	 * address yet.
	 *
	 * @return true if the address was inherited, false if the view
	 * already had an address or if the specified address is empty
	 */
	bool InheritAddress(AllocatorPtr alloc,
			    const ResourceAddress &src) noexcept;

	/**
	 * Inherit the address and other related settings from one view to
	 * another.
	 *
	 * @return true if attributes were inherited, false if the destination
	 * view already had an address or if the source view's address is
	 * empty
	 */
	bool InheritFrom(AllocatorPtr alloc, const WidgetView &src) noexcept;

	/**
	 * Does the effective view enable the HTML processor?
	 */
	gcc_pure
	bool HasProcessor() const noexcept;

	/**
	 * Is this view a container?
	 */
	gcc_pure
	bool IsContainer() const noexcept;

	/**
	 * Does this view need to be expanded with widget_view_expand()?
	 */
	gcc_pure
	bool IsExpandable() const noexcept;

	/**
	 * Expand the strings in this view (not following the linked list)
	 * with the specified regex result.
	 *
	 * Throws std::runtime_error on error.
	 */
	void Expand(AllocatorPtr alloc, const MatchInfo &match_info) noexcept;
};

/**
 * Finds a view by its name.  If name==NULL, it returns the first
 * view.
 */
gcc_pure
const WidgetView *
widget_view_lookup(const WidgetView *view, const char *name) noexcept;

/**
 * Does any view in the linked list need to be expanded with
 * widget_view_expand()?
 */
gcc_pure
bool
widget_view_any_is_expandable(const WidgetView *view) noexcept;

/**
 * The same as widget_view_expand(), but expand all voews in
 * the linked list.
 */
void
widget_view_expand_all(AllocatorPtr alloc, WidgetView *view,
		       const MatchInfo &match_info) noexcept;
