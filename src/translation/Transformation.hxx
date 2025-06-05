// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "FilterTransformation.hxx"
#include "util/IntrusiveForwardList.hxx"

#include <new>
#include <type_traits>

class AllocatorPtr;

struct XmlProcessorTransformation {
	unsigned options;
};

struct CssProcessorTransformation {
	unsigned options;
};

struct TextProcessorTransformation {
};

/**
 * Transformations which can be applied to resources.
 */
struct Transformation : IntrusiveForwardListHook {
	enum class Type {
		PROCESS,
		PROCESS_CSS,
		PROCESS_TEXT,
		FILTER,
	} type;

	union U {
		XmlProcessorTransformation processor;

		CssProcessorTransformation css_processor;

		TextProcessorTransformation text_processor;

		FilterTransformation filter;

		/* we don't even try to call destructors in this union, and
		   these assertions ensure that this is safe: */
		static_assert(std::is_trivially_destructible<XmlProcessorTransformation>::value);
		static_assert(std::is_trivially_destructible<CssProcessorTransformation>::value);
		static_assert(std::is_trivially_destructible<TextProcessorTransformation>::value);
		static_assert(std::is_trivially_destructible<FilterTransformation>::value);

		/**
		 * This constructor declaration is necessary to allow
		 * non-trivial member types.
		 */
		U() noexcept {}
	} u;

	explicit Transformation(XmlProcessorTransformation &&src) noexcept
		:type(Type::PROCESS) {
		new(&u.processor) XmlProcessorTransformation(std::move(src));
	}

	explicit Transformation(CssProcessorTransformation &&src) noexcept
		:type(Type::PROCESS_CSS) {
		new(&u.processor) CssProcessorTransformation(std::move(src));
	}

	explicit Transformation(TextProcessorTransformation &&src) noexcept
		:type(Type::PROCESS_TEXT) {
		new(&u.processor) TextProcessorTransformation(std::move(src));
	}

	explicit Transformation(FilterTransformation &&src) noexcept
		:type(Type::FILTER) {
		new(&u.filter) FilterTransformation(std::move(src));
	}

	Transformation(AllocatorPtr alloc, const Transformation &src) noexcept;

	Transformation(const Transformation &) = delete;
	Transformation &operator=(const Transformation &) = delete;

	/**
	 * Returns true if the chain contains at least one "PROCESS"
	 * transformation.
	 */
	[[gnu::pure]]
	static bool HasProcessor(const IntrusiveForwardList<Transformation> &list) noexcept;

	/**
	 * Returns true if the first "PROCESS" transformation in the chain (if
	 * any) includes the "CONTAINER" processor option.
	 */
	[[gnu::pure]]
	static bool IsContainer(const IntrusiveForwardList<Transformation> &list) noexcept;

	/**
	 * Does this transformation need to be expanded with
	 * transformation_expand()?
	 */
	[[gnu::pure]]
	bool IsExpandable() const noexcept {
		return type == Type::FILTER &&
			u.filter.IsExpandable();
	}

	/**
	 * Does any transformation in the linked list need to be expanded with
	 * transformation_expand()?
	 */
	[[gnu::pure]]
	static bool IsChainExpandable(const IntrusiveForwardList<Transformation> &list) noexcept;

	[[gnu::malloc]]
	Transformation *Dup(AllocatorPtr alloc) const noexcept;

	static IntrusiveForwardList<Transformation> DupChain(AllocatorPtr alloc,
							     const IntrusiveForwardList<Transformation> &src) noexcept;

	/**
	 * Expand the strings in this transformation (not following the linked
	 * lits) with the specified regex result.
	 *
	 * Throws std::runtime_error on error.
	 */
	void Expand(AllocatorPtr alloc, const MatchData &match_data);

	/**
	 * The same as Expand(), but expand all transformations in the
	 * linked list.
	 */
	static void ExpandChain(AllocatorPtr alloc,
				IntrusiveForwardList<Transformation> &list,
				const MatchData &match_data);
};
