/*
 * Copyright 2007-2022 CM4all GmbH
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

#include "SubstIstream.hxx"
#include "FacadeIstream.hxx"
#include "UnusedPtr.hxx"
#include "New.hxx"
#include "Bucket.hxx"
#include "pool/pool.hxx"
#include "util/DestructObserver.hxx"
#include "util/SpanCast.hxx"
#include "util/StringSplit.hxx"

#include <assert.h>
#include <string.h>

/* ternary search tree */
struct SubstNode {
	SubstNode *parent, *left, *right, *equals;
	char ch;

	struct {
		const char *a;
		size_t b_length;
		char b[1];

		constexpr std::span<const char> AsSpan() const noexcept {
			return {b, b_length};
		}
	} leaf;
};

class SubstIstream final : public FacadeIstream, DestructAnchor {
	bool had_input, had_output;

	bool send_first;

	SubstTree tree;

	const SubstNode *match;
	std::span<const std::byte> mismatch{};

	enum class State {
		/** searching the first matching character */
		NONE,

		/** at least the first character was found, checking for the
		    rest */
		MATCH,

		/** inserting the substitution */
		INSERT,
	} state = State::NONE;
	size_t a_match, b_sent;

public:
	SubstIstream(struct pool &p, UnusedIstreamPtr &&_input, SubstTree &&_tree) noexcept
		:FacadeIstream(p, std::move(_input)), tree(std::move(_tree)) {}

private:
	/** find the first occurence of a "first character" in the buffer */
	const char *FindFirstChar(const char *data, size_t length) noexcept;

	/**
	 * Write data from "b".
	 *
	 * @return the number of bytes remaining
	 */
	size_t TryWriteB() noexcept;

	bool FeedMismatch() noexcept;
	bool WriteMismatch() noexcept;

	/**
	 * Forwards source data to the istream handler.
	 *
	 * @return (size_t)-1 when everything has been consumed, or the
	 * correct return value for the data() callback.
	 */
	size_t ForwardSourceData(const char *start,
				 const char *p, size_t length) noexcept;
	size_t ForwardSourceDataFinal(const char *start,
				      const char *end, const char *p) noexcept;

	size_t Feed(std::span<const std::byte> src) noexcept;

public:
	/* virtual methods from class Istream */

	void _Read() noexcept override;
	void _FillBucketList(IstreamBucketList &list) override;
	size_t _ConsumeBucketList(size_t nbytes) noexcept override;

	/* istream handler */

	size_t OnData(std::span<const std::byte> src) noexcept override;

	void OnEof() noexcept override;
	void OnError(std::exception_ptr ep) noexcept override;
};

/*
 * helper functions
 *
 */

/** find a character in the tree */
[[gnu::pure]]
static const SubstNode *
subst_find_char(const SubstNode *node, char ch) noexcept
{
	assert(node != nullptr);

	if (ch == 0)
		/* we cannot support null bytes */
		return nullptr;

	do {
		if (node->ch == ch) {
			assert(node->equals != nullptr);
			return node->equals;
		}

		if (ch < node->ch)
			node = node->left;
		else
			node = node->right;
	} while (node != nullptr);

	return nullptr;
}

/** find the leaf ending the current search word */
[[gnu::pure]]
static const SubstNode *
subst_find_leaf(const SubstNode *node) noexcept
{
	assert(node != nullptr);

	do {
		if (node->ch == 0)
			return node;

		if (0 < node->ch)
			node = node->left;
		else
			node = node->right;
	} while (node != nullptr);

	return nullptr;
}

/**
 * Check whether the given input can possibly be a match.
 *
 * @param match the current match (usually found with memchr() by
 * SubstTree::FindFirstChar())
 * @param input the remaining input
 * @param true if the remaining input matches so far (but may not be
 * complete yet), false if the input cannot match
 */
[[gnu::pure]]
static bool
CheckMatch(const SubstNode *match, std::string_view input) noexcept
{
	for (const char ch : input) {
		if (subst_find_leaf(match) != nullptr)
			return true;

		match = subst_find_char(match, ch);
		if (match == nullptr)
			return false;
	}

	return true;
}

/** find any leaf which begins with the current partial match, used to
    find a buffer which is partially re-inserted into the data
    stream */
[[gnu::pure]]
static const SubstNode *
subst_find_any_leaf(const SubstNode *node) noexcept
{
	while (true) {
		assert(node != nullptr);

		if (node->ch == 0)
			return node;

		node = node->equals;
	}
}

/** iterates over the current depth */
[[gnu::pure]]
static const SubstNode *
subst_next_non_leaf_node(const SubstNode *node, const SubstNode *root) noexcept
{
	/* dive into left wing first */
	if (node->left != nullptr && node->left->ch != 0)
		return node->left;

	/* if left does not exist, go right */
	if (node->right != nullptr && node->right->ch != 0)
		return node->right;

	/* this subtree is finished, go up */
	while (true) {
		/* don't go above our root */
		if (node == root)
			return nullptr;

		assert(node->parent != nullptr);

		if (node->parent->left == node) {
			node = node->parent;

			/* only go to parent->right if we came from
			   parent->left */
			if (node->right != nullptr && node->right->ch != 0)
				return node;
		} else {
			node = node->parent;
		}
	}
}

inline std::pair<const SubstNode *, const char *>
SubstTree::FindFirstChar(const char *data, size_t length) const noexcept
{
	const char *const end = data + length;
	const SubstNode *n = root;
	const SubstNode *match = nullptr;
	const char *min = nullptr;

	while (n != nullptr) {
		assert(n->ch != 0);

		/* loop to find all instances of this start character, until
		   there is one where the rest also matches */
		const char *p = data;
		while (true) {
			p = (const char *)memchr(p, n->ch, end - p);
			if (p != nullptr && (min == nullptr || p < min)) {
				if (!CheckMatch(n->equals, {p + 1, end})) {
					/* late mismatch; continue the loop to check if
					   there are more of the current start
					   character */
					++p;
					continue;
				}

				assert(n->equals != nullptr);
				match = n->equals;
				min = p;
			}

			break;
		}

		/* check the next start character in the #SubstTree */
		n = subst_next_non_leaf_node(n, root);
	}

	return std::make_pair(match, min);
}

inline const char *
SubstIstream::FindFirstChar(const char *data, size_t length) noexcept
{
	auto x = tree.FindFirstChar(data, length);
	match = x.first;
	return x.second;
}

size_t
SubstIstream::TryWriteB() noexcept
{
	assert(state == State::INSERT);
	assert(a_match > 0);
	assert(match != nullptr);
	assert(match->ch == 0);
	assert(a_match == strlen(match->leaf.a));

	const auto src = std::as_bytes(match->leaf.AsSpan().subspan(b_sent));
	assert(!src.empty());

	const size_t nbytes = InvokeData(src);
	assert(nbytes <= src.size());
	if (nbytes > 0) {
		/* note progress */
		b_sent += nbytes;

		/* finished sending substitution? */
		if (nbytes == src.size())
			state = State::NONE;
	}

	return src.size() - nbytes;
}

bool
SubstIstream::FeedMismatch() noexcept
{
	assert(state == State::NONE);
	assert(input.IsDefined());
	assert(!mismatch.empty());

	if (send_first) {
		const size_t nbytes = InvokeData(mismatch.first(1));
		if (nbytes == 0)
			return true;

		mismatch = mismatch.subspan(nbytes);

		if (mismatch.empty())
			return false;

		send_first = false;
	}

	const size_t nbytes = Feed(mismatch);
	if (nbytes == 0)
		return true;

	assert(nbytes <= mismatch.size());
	mismatch = mismatch.subspan(nbytes);

	return !mismatch.empty();
}

bool
SubstIstream::WriteMismatch() noexcept
{
	assert(!input.IsDefined() || state == State::NONE);
	assert(!mismatch.empty());

	size_t nbytes = InvokeData(mismatch);
	if (nbytes == 0)
		return true;

	assert(nbytes <= mismatch.size());
	mismatch = mismatch.subspan(nbytes);

	if (!mismatch.empty())
		return true;

	if (!input.IsDefined()) {
		DestroyEof();
		return true;
	}

	return false;
}

size_t
SubstIstream::ForwardSourceData(const char *start,
				const char *p, size_t length) noexcept
{
	const DestructObserver destructed(*this);

	size_t nbytes = InvokeData(std::as_bytes(std::span{p, length}));
	if (destructed) {
		/* stream has been closed - we must return 0 */
		assert(nbytes == 0);
		return 0;
	}

	had_output = true;

	if (nbytes < length) {
		/* blocking */
		state = State::NONE;
		return (p - start) + nbytes;
	} else
		/* everything has been consumed */
		return (size_t)-1;
}

inline size_t
SubstIstream::ForwardSourceDataFinal(const char *start,
				     const char *end, const char *p) noexcept
{
	const DestructObserver destructed(*this);

	size_t nbytes = InvokeData(std::as_bytes(std::span{p, end}));
	if (nbytes > 0 || !destructed) {
		had_output = true;
		nbytes += (p - start);
	}

	return nbytes;
}

size_t
SubstIstream::Feed(std::span<const std::byte> src) noexcept
{
	assert(input.IsDefined());

	const DestructObserver destructed(*this);

	const char *const data0 = (const char *)src.data(), *data = data0, *p = data0,
		*const end = p + src.size(), *first = nullptr;
	const SubstNode *n;

	had_input = true;

	/* find new match */

	do {
		assert(data >= data0);
		assert(p >= data);
		assert(p <= end);

		switch (state) {
		case State::NONE:
			/* find matching first char */

			assert(first == nullptr);

			first = FindFirstChar(p, end - p);
			if (first == nullptr)
				/* no match, try to write and return */
				return ForwardSourceDataFinal(data0, end, data);

			state = State::MATCH;
			a_match = 1;

			p = first + 1;

			/* XXX check if match is full */
			break;

		case State::MATCH:
			/* now see if the rest matches; note that max_compare may be
			   0, but that isn't a problem */

			n = subst_find_char(match, *p);
			if (n != nullptr) {
				/* next character matches */

				++a_match;
				++p;
				match = n;

				n = subst_find_leaf(n);
				if (n != nullptr) {
					/* full match */

					match = n;

					if (first != nullptr && first > data) {
						/* write the data chunk before the match */

						had_output = true;

						const size_t chunk_length = first - data;
						const size_t nbytes =
							ForwardSourceData(data0, data, chunk_length);
						if (nbytes != (size_t)-1)
							return nbytes;
					}

					/* move data pointer */

					data = p;
					first = nullptr;

					/* switch state */

					if (n->leaf.b_length > 0) {
						state = State::INSERT;
						b_sent = 0;
					} else {
						state = State::NONE;
					}
				}
			} else {
				/* mismatch. reset match indicator and find new one */

				if (first != nullptr && (first > data ||
							 !mismatch.empty())) {
					/* write the data chunk before the (mis-)match */

					had_output = true;

					size_t chunk_length = first - data;
					if (!mismatch.empty())
						++chunk_length;

					const size_t nbytes =
						ForwardSourceData(data0, data, chunk_length);
					if (nbytes != (size_t)-1)
						return nbytes;
				} else {
					/* when re-parsing a mismatch, "first" must not be
					   nullptr because we entered this function with
					   state=NONE */
					assert(mismatch.empty());
				}

				/* move data pointer */

				data = p;
				first = nullptr;

				/* switch state */

				/* seek any leaf to get a valid match->leaf.a which we
				   can use to re-insert the partial match into the
				   stream */

				state = State::NONE;

				if (mismatch.empty()) {
					send_first = true;

					n = subst_find_any_leaf(match);
					assert(n != nullptr);
					assert(n->ch == 0);
					mismatch = std::as_bytes(std::span{n->leaf.a, a_match});

					if (FeedMismatch())
						return destructed ? 0 : data - data0;
				}
			}

			break;

		case State::INSERT:
			/* there is a previous full match, copy data from b */

			const size_t nbytes = TryWriteB();
			if (nbytes > 0) {
				if (destructed)
					return 0;

				assert(state == State::INSERT);
				/* blocking */
				return data - data0;
			}

			assert(state == State::NONE);

			break;
		}
	} while (p < end || state == State::INSERT);

	size_t chunk_length;
	if (first != nullptr)
		/* we have found a partial match which we discard now, instead
		   we will write the chunk right before this match */
		chunk_length = first - data;
	else if (state == State::MATCH || state == State::INSERT)
		chunk_length = 0;
	else
		/* there was no match (maybe a partial match which mismatched
		   at a later stage): pass everything */
		chunk_length = end - data;

	if (chunk_length > 0) {
		/* write chunk */

		had_output = true;

		const size_t nbytes = ForwardSourceData(data0, data, chunk_length);
		if (nbytes != (size_t)-1)
			return nbytes;
	}

	return p - data0;
}

/*
 * istream handler
 *
 */

inline size_t
SubstIstream::OnData(std::span<const std::byte> src) noexcept
{
	if (!mismatch.empty() && FeedMismatch())
		return 0;

	return Feed(src);
}

void
SubstIstream::OnEof() noexcept
{
	assert(input.IsDefined());

	input.Clear();

	switch (state) {
		size_t nbytes;

	case State::NONE:
		break;

	case State::MATCH:
		/* we're in the middle of a match, technically making this a
		   mismatch because we reach end of file before end of
		   match */
		if (mismatch.empty()) {
			const SubstNode *n = subst_find_any_leaf(match);
			assert(n != nullptr);
			assert(n->ch == 0);

			mismatch = std::as_bytes(std::span{n->leaf.a, a_match});
			WriteMismatch();
			return;
		}
		break;

	case State::INSERT:
		nbytes = TryWriteB();
		if (nbytes > 0)
			return;
		break;
	}

	if (state == State::NONE)
		DestroyEof();
}

void
SubstIstream::OnError(std::exception_ptr ep) noexcept
{
	assert(input.IsDefined());

	input.Clear();
	DestroyError(ep);
}

/*
 * istream implementation
 *
 */

void
SubstIstream::_Read() noexcept
{
	if (!mismatch.empty()) {
		bool ret = input.IsDefined()
			? FeedMismatch()
			: WriteMismatch();

		if (ret || !input.IsDefined())
			return;
	} else {
		assert(input.IsDefined());
	}

	switch (state) {
		size_t nbytes;

	case State::NONE:
	case State::MATCH: {
		assert(input.IsDefined());

		had_output = false;

		const DestructObserver destructed(*this);

		do {
			had_input = false;
			input.Read();
		} while (!destructed && input.IsDefined() && had_input &&
			 !had_output && state != State::INSERT);

		return;
	}

	case State::INSERT:
		nbytes = TryWriteB();
		if (nbytes > 0)
			return;
		break;
	}

	if (state == State::NONE && !input.IsDefined())
		DestroyEof();
}

void
SubstIstream::_FillBucketList(IstreamBucketList &list)
{
	if (!mismatch.empty()) {
		if (input.IsDefined()) {
			// FeedMismatch()

			if (send_first)
				list.Push(mismatch.first(1));

			// TODO: re-parse the rest of the mismatch buffer
			list.SetMore();
			return;
		} else {
			// WriteMismatch()
			list.Push(mismatch);
			return;
		}
	} else {
		assert(input.IsDefined());
	}

	switch (state) {
	case State::NONE: {
		IstreamBucketList tmp;

		try {
			input.FillBucketList(tmp);
		} catch (...) {
			input.Clear();
			Destroy();
			throw;
		}

		if (tmp.HasMore())
			list.SetMore();

		for (const auto &bucket : tmp) {
			if (!bucket.IsBuffer()) {
				list.SetMore();
				return;
			}

			auto s = ToStringView(bucket.GetBuffer());

			const char *first = FindFirstChar(s.data(),
							  s.size());
			if (first != nullptr) {
				s = Partition(s, first).first;
				if (!s.empty())
					list.Push(AsBytes(s));
				list.SetMore();
				return;
			}

			list.Push(AsBytes(s));
		}

		return;
	}

	case State::MATCH:
		// TODO: read from input
		list.SetMore();
		return;

	case State::INSERT: {
		// TryWriteB
		assert(state == State::INSERT);
		assert(a_match > 0);
		assert(match != nullptr);
		assert(match->ch == 0);
		assert(a_match == strlen(match->leaf.a));

		const size_t length = match->leaf.b_length - b_sent;
		assert(length > 0);

		list.Push({(const std::byte *)match->leaf.b + b_sent, length});
		list.SetMore();

		// TODO: read more
		return;
	}
	}
}

size_t
SubstIstream::_ConsumeBucketList(size_t nbytes) noexcept
{
	assert(nbytes > 0);

	if (!mismatch.empty()) {
		if (input.IsDefined()) {
			// FeedMismatch()

			if (send_first) {
				send_first = false;
				return Consumed(1);
			}

			return 0;
		} else {
			// WriteMismatch()
			nbytes = std::min(nbytes, mismatch.size());
			mismatch = mismatch.subspan(nbytes);
			return Consumed(nbytes);
		}
	} else {
		assert(input.IsDefined());
	}

	switch (state) {
	case State::NONE:
		return Consumed(input.ConsumeBucketList(nbytes));

	case State::MATCH:
		return 0;

	case State::INSERT: {
		// TryWriteB
		assert(state == State::INSERT);
		assert(a_match > 0);
		assert(match != nullptr);
		assert(match->ch == 0);
		assert(a_match == strlen(match->leaf.a));

		const size_t length = match->leaf.b_length - b_sent;
		assert(length > 0);

		size_t consumed = std::min(nbytes, length);

		/* note progress */
		b_sent += consumed;

		/* finished sending substitution? */
		if (consumed == length)
			state = State::NONE;
		return Consumed(consumed);
	}
	}

	gcc_unreachable();
}

/*
 * constructor
 *
 */

UnusedIstreamPtr
istream_subst_new(struct pool *pool, UnusedIstreamPtr input,
		  SubstTree tree) noexcept
{
	return NewIstreamPtr<SubstIstream>(*pool, std::move(input),
					   std::move(tree));
}

bool
SubstTree::Add(struct pool &pool, const char *a0, std::string_view b) noexcept
{
	SubstNode *parent = nullptr;
	const char *a = a0;

	assert(a0 != nullptr);
	assert(*a0 != 0);

	auto **pp = &root;
	do {
		auto *p = *pp;
		if (p == nullptr) {
			/* create new tree node */

			p = (SubstNode *)p_malloc(&pool, sizeof(*p) - sizeof(p->leaf));
			p->parent = parent;
			p->left = nullptr;
			p->right = nullptr;
			p->equals = nullptr;
			p->ch = *a++;

			*pp = parent = p;
			pp = &p->equals;
		} else if (*a < p->ch) {
			pp = &p->left;
			parent = p;
		} else if (*a > p->ch) {
			pp = &p->right;
			parent = p;
		} else {
			/* tree node exists and matches, enter new level (next
			   character) */
			pp = &p->equals;
			parent = p;
			++a;
		}
	} while (*a);

	/* this keyword already exists */
	if (*pp != nullptr)
		return false;

	/* create new leaf node */

	SubstNode *p = (SubstNode *)
		p_malloc(&pool, sizeof(*p) + b.size() - sizeof(p->leaf.b));
	p->parent = parent;
	p->left = nullptr;
	p->right = nullptr;
	p->equals = nullptr;
	p->ch = 0;
	p->leaf.a = a0;
	p->leaf.b_length = b.size();
	std::copy(b.begin(), b.end(), p->leaf.b);

	*pp = p;

	return true;
}

bool
SubstTree::Add(struct pool &pool, const char *a0, const char *b) noexcept
{
	return Add(pool, a0,
		   b != nullptr ? std::string_view{b} : std::string_view{});
}
