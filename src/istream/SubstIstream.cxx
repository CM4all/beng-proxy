// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "SubstIstream.hxx"
#include "FacadeIstream.hxx"
#include "UnusedPtr.hxx"
#include "New.hxx"
#include "Bucket.hxx"
#include "pool/pool.hxx"
#include "util/DestructObserver.hxx"
#include "util/SpanCast.hxx"
#include "util/StringSplit.hxx"

#include <cassert>
#include <utility> // for std::unreachable()

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

	constexpr bool IsLeaf() const noexcept {
		return ch == 0;
	}

	/** find a character in the tree */
	[[gnu::pure]]
	static const SubstNode *FindChar(const SubstNode *node, char ch) noexcept;

	/** find the leaf ending the current search word */
	[[gnu::pure]]
	static const SubstNode *FindLeaf(const SubstNode *node) noexcept;

	/**
	 * Find any leaf which begins with the current partial match,
	 * used to find a buffer which is partially re-inserted into
	 * the data stream.
	 */
	[[gnu::pure]]
	static const SubstNode *FindAnyLeaf(const SubstNode *node) noexcept;

	/** iterates over the current depth */
	[[gnu::pure]]
	static const SubstNode *NextNonLeafNode(const SubstNode *node, const SubstNode *root) noexcept;
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
	static bool CheckMatch(const SubstNode *match, std::string_view input) noexcept;

	/**
	 * Return the string that lead to a partial match.  This is
	 * used by the caller to re-insert the original text without
	 * having access to input data.
	 */
	[[gnu::pure]]
	std::string_view GetPartialMatchString(std::size_t length) const noexcept {
		const auto *leaf_node = SubstNode::FindAnyLeaf(this);
		assert(leaf_node != nullptr);
		assert(leaf_node->IsLeaf());

		return {leaf_node->leaf.a, length};
	}
};

class SubstIstream final : public FacadeIstream, DestructAnchor {
	bool had_input, had_output;

	SubstTree tree;

	enum class State {
		/** searching the first matching character */
		NONE,

		/** at least the first character was found, checking for the
		    rest */
		MATCH,

		/** inserting the substitution */
		INSERT,
	};

	struct BufferAnalysis {
		/**
		 * If #state is #MATCH, then this is the non-leaf
		 * #SubstNode matching the most recent character.
		 *
		 * If #state is #INSERT, then this is the leaf
		 * #SubstNode containing the replacement string ("b").
		 */
		const SubstNode *match;

		/**
		 * If non-empty, then this should be parsed instead of
		 * data from our #input.  It is set after a mismatch
		 * and contains the portion of "a" that has matched.
		 * See also #send_first.
		 */
		std::span<const std::byte> mismatch{};

		/**
		 * Number of bytes our input has matched "a" so far.
		 * Only initialized (to a positive value) if #state is
		 * #MATCH.
		 */
		std::size_t a_match;

		/**
		 * Number of bytes of "b" that were already submitted
		 * to our #IstreamHandler.  Only initialized if #state
		 * is #INSERT.
		 */
		std::size_t b_sent;

		State state = State::NONE;

		/**
		 * If true, then the first byte of #mismatch will be
		 * submitted to our #IstreamHandler instead of feeding
		 * it into the parser (via Feed()).  This is necessary
		 * to avoid encountering the same mismatch again.
		 */
		bool send_first;

		/**
		 * @return true if there is more mismatch data, false
		 * if the mismatch is now empty
		 */
		constexpr bool ConsumeMismatch(std::size_t nbytes) noexcept {
			assert(nbytes <= mismatch.size());

			mismatch = mismatch.subspan(nbytes);
			return !mismatch.empty();
		}

		/**
		 * @return the number of bytes actually consumed
		 */
		constexpr std::size_t ClampConsumeMismatch(std::size_t nbytes) noexcept {
			if (nbytes > mismatch.size())
				nbytes = mismatch.size();
			mismatch = mismatch.subspan(nbytes);
			return nbytes;
		}

		constexpr std::span<const std::byte> GetB() const noexcept {
			assert(state == State::INSERT);
			assert(match != nullptr);
			assert(b_sent < match->leaf.b_length);

			return std::as_bytes(match->leaf.AsSpan().subspan(b_sent));
		}
	};

	BufferAnalysis analysis;

	/**
	 * How many bytes have previously been returned in buckets?
	 * This is used for implementing _GetLength().  It is set
	 * by _FillBucketList() and must be updated by calling
	 * SubtractBucketAvailable() or BucketConsumed().
	 *
	 * TODO implement properly
	 */
	std::size_t bucket_available = 0;

public:
	SubstIstream(struct pool &p, UnusedIstreamPtr &&_input, SubstTree &&_tree) noexcept
		:FacadeIstream(p, std::move(_input)), tree(std::move(_tree)) {}

private:
	/**
	 * Submit filtered data to our #IstreamHandler (i.e. wrapper
	 * for InvokeData() with some bookkeeping).
	 *
	 * @return the number of bytes consumed
	 */
	std::size_t FeedOutput(std::span<const std::byte> src) noexcept {
		had_output = true;

		const std::size_t nbytes = InvokeData(src);
		if (nbytes > 0)
			SubtractBucketAvailable(nbytes);

		return nbytes;
	}

	void UpdateBucketAvailable(const IstreamBucketList &list) noexcept {
		if (auto total = list.GetTotalBufferSize(); total > bucket_available)
			bucket_available = total;
	}

	std::size_t SubtractBucketAvailable(std::size_t nbytes) noexcept {
		if (nbytes <= bucket_available)
			bucket_available -= nbytes;
		else
			bucket_available = 0;
		return nbytes;
	}

	std::size_t BucketConsumed(std::size_t nbytes) noexcept {
		return Consumed(SubtractBucketAvailable(nbytes));
	}

	ConsumeBucketResult BucketConsumed(ConsumeBucketResult result) noexcept {
		SubtractBucketAvailable(result.consumed);
		return Consumed(result);
	}

	/** find the first occurence of a "first character" in the buffer */
	const char *FindFirstChar(std::string_view src) noexcept;

	/**
	 * Write data from "b".
	 *
	 * @return the number of bytes remaining
	 */
	size_t TryWriteB() noexcept;

	/**
	 * Feed the partial match after a mismatch to the parser (to
	 * search for more matches).
	 *
	 * @return true if there is more mismatch data, false if the
	 * mismatch is now empty
	 */
	bool FeedMismatch() noexcept;

	/**
	 * Submit the partial match after a mismatch to the handler.
	 *
	 * @return true if there is more mismatch data, false if the
	 * mismatch is now empty
	 */
	bool WriteMismatch() noexcept;

	/**
	 * Forwards source data to the istream handler.
	 *
	 * @return (size_t)-1 when everything has been consumed, or the
	 * correct return value for the data() callback.
	 */
	size_t ForwardSourceData(const char *start,
				 std::string_view src,
				 const DestructObserver &destructed) noexcept;

	/**
	 * Like ForwardSourceData(), but for the final input section
	 * where no match was found.
	 */
	size_t ForwardSourceDataFinal(const char *start,
				      const char *end, const char *p,
				      const DestructObserver &destructed) noexcept;

	/**
	 * Feed input data to the parser.
	 *
	 * @return the number of #src bytes consumed (0 if this object
	 * has been closed)
	 */
	size_t Feed(std::span<const std::byte> src) noexcept;

public:
	/* virtual methods from class Istream */

	void _Read() noexcept override;
	void _FillBucketList(IstreamBucketList &list) override;
	ConsumeBucketResult _ConsumeBucketList(size_t nbytes) noexcept override;

	/* istream handler */

	IstreamLength _GetLength() noexcept override {
		return {
			.length = bucket_available,
			.exhaustive = false,
		};
	}

	size_t OnData(std::span<const std::byte> src) noexcept override;

	void OnEof() noexcept override;
	void OnError(std::exception_ptr &&ep) noexcept override;
};

/*
 * helper functions
 *
 */

inline const SubstNode *
SubstNode::FindChar(const SubstNode *node, char ch) noexcept
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

inline const SubstNode *
SubstNode::FindLeaf(const SubstNode *node) noexcept
{
	assert(node != nullptr);

	do {
		if (node->IsLeaf())
			return node;

		if (0 < node->ch)
			node = node->left;
		else
			node = node->right;
	} while (node != nullptr);

	return nullptr;
}

inline bool
SubstNode::CheckMatch(const SubstNode *match, std::string_view input) noexcept
{
	for (const char ch : input) {
		if (SubstNode::FindLeaf(match) != nullptr)
			return true;

		match = SubstNode::FindChar(match, ch);
		if (match == nullptr)
			return false;
	}

	return true;
}

inline const SubstNode *
SubstNode::FindAnyLeaf(const SubstNode *node) noexcept
{
	while (true) {
		assert(node != nullptr);

		if (node->IsLeaf())
			return node;

		node = node->equals;
	}
}

inline const SubstNode *
SubstNode::NextNonLeafNode(const SubstNode *node, const SubstNode *root) noexcept
{
	/* dive into left wing first */
	if (node->left != nullptr && !node->left->IsLeaf())
		return node->left;

	/* if left does not exist, go right */
	if (node->right != nullptr && !node->right->IsLeaf())
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
			if (node->right != nullptr && !node->right->IsLeaf())
				return node;
		} else {
			node = node->parent;
		}
	}
}

inline std::pair<const SubstNode *, const char *>
SubstTree::FindFirstChar(std::string_view src) const noexcept
{
	const char *const end = src.data() + src.size();
	const SubstNode *n = root;
	const SubstNode *match = nullptr;
	const char *min = nullptr;

	while (n != nullptr) {
		assert(!n->IsLeaf());

		/* loop to find all instances of this start character, until
		   there is one where the rest also matches */
		const char *p = src.data();
		while (true) {
			p = (const char *)memchr(p, n->ch, end - p);
			if (p != nullptr && (min == nullptr || p < min)) {
				if (!SubstNode::CheckMatch(n->equals, {p + 1, end})) {
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
		n = SubstNode::NextNonLeafNode(n, root);
	}

	return std::make_pair(match, min);
}

inline const char *
SubstIstream::FindFirstChar(std::string_view src) noexcept
{
	auto x = tree.FindFirstChar(src);
	analysis.match = x.first;
	return x.second;
}

size_t
SubstIstream::TryWriteB() noexcept
{
	assert(analysis.state == State::INSERT);
	assert(analysis.a_match > 0);
	assert(analysis.match != nullptr);
	assert(analysis.match->IsLeaf());
	assert(analysis.a_match == strlen(analysis.match->leaf.a));

	const auto src = analysis.GetB();
	assert(!src.empty());

	const std::size_t nbytes = FeedOutput(src);
	assert(nbytes <= src.size());
	if (nbytes > 0) {
		/* note progress */
		analysis.b_sent += nbytes;

		/* finished sending substitution? */
		if (nbytes == src.size())
			analysis.state = State::NONE;
	}

	return src.size() - nbytes;
}

bool
SubstIstream::FeedMismatch() noexcept
{
	assert(analysis.state == State::NONE);
	assert(input.IsDefined());
	assert(!analysis.mismatch.empty());

	if (analysis.send_first) {
		const size_t nbytes = FeedOutput(analysis.mismatch.first(1));
		if (nbytes == 0)
			return true;

		if (!analysis.ConsumeMismatch(nbytes))
			return false;

		analysis.send_first = false;
	}

	const size_t nbytes = Feed(analysis.mismatch);
	if (nbytes == 0)
		return true;

	return analysis.ConsumeMismatch(nbytes);
}

bool
SubstIstream::WriteMismatch() noexcept
{
	assert(!input.IsDefined());
	assert(!analysis.mismatch.empty());

	size_t nbytes = FeedOutput(analysis.mismatch);
	if (nbytes == 0)
		return true;

	if (analysis.ConsumeMismatch(nbytes))
		return true;

	return false;
}

size_t
SubstIstream::ForwardSourceData(const char *start,
				std::string_view src,
				const DestructObserver &destructed) noexcept
{
	size_t nbytes = FeedOutput(AsBytes(src));
	if (destructed) {
		/* stream has been closed - we must return 0 */
		assert(nbytes == 0);
		return 0;
	}

	if (nbytes < src.size()) {
		/* blocking */
		analysis.state = State::NONE;
		return (src.data() - start) + nbytes;
	} else
		/* everything has been consumed */
		return (size_t)-1;
}

inline size_t
SubstIstream::ForwardSourceDataFinal(const char *start,
				     const char *end, const char *p,
				     const DestructObserver &destructed) noexcept
{
	size_t nbytes = FeedOutput(std::as_bytes(std::span{p, end}));
	if (nbytes > 0 || !destructed) {
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

	had_input = true;

	/* find new match */

	do {
		assert(data >= data0);
		assert(p >= data);
		assert(p <= end);

		switch (analysis.state) {
		case State::NONE:
			/* find matching first char */

			assert(first == nullptr);

			first = FindFirstChar({p, end});
			if (first == nullptr)
				/* no match, try to write and return */
				return ForwardSourceDataFinal(data0, end, data,
							      destructed);

			analysis.state = State::MATCH;
			analysis.a_match = 1;

			p = first + 1;

			/* XXX check if match is full */
			break;

		case State::MATCH:
			/* now see if the rest matches; note that max_compare may be
			   0, but that isn't a problem */

			if (const auto *ch = SubstNode::FindChar(analysis.match, *p)) {
				/* next character matches */

				++analysis.a_match;
				++p;
				analysis.match = ch;

				if (const auto *leaf = SubstNode::FindLeaf(ch)) {
					/* full match */

					analysis.match = leaf;

					if (first != nullptr && first > data) {
						/* write the data chunk before the match */

						const size_t nbytes =
							ForwardSourceData(data0, {data, first},
									  destructed);
						if (nbytes != (size_t)-1)
							return nbytes;
					}

					/* move data pointer */

					data = p;
					first = nullptr;

					/* switch state */

					if (leaf->leaf.b_length > 0) {
						analysis.state = State::INSERT;
						analysis.b_sent = 0;
					} else {
						analysis.state = State::NONE;
					}
				}
			} else {
				/* mismatch. reset match indicator and find new one */

				if (first != nullptr && (first > data ||
							 !analysis.mismatch.empty())) {
					/* write the data chunk before the (mis-)match */

					const char *chunk_end = first;
					if (!analysis.mismatch.empty())
						++chunk_end;

					const size_t nbytes =
						ForwardSourceData(data0, {data, chunk_end},
								  destructed);
					if (nbytes != (size_t)-1)
						return nbytes;
				} else {
					/* when re-parsing a mismatch, "first" must not be
					   nullptr because we entered this function with
					   state=NONE */
					assert(analysis.mismatch.empty());
				}

				/* move data pointer */

				data = p;
				first = nullptr;

				/* switch state */

				/* seek any leaf to get a valid match->leaf.a which we
				   can use to re-insert the partial match into the
				   stream */

				analysis.state = State::NONE;

				if (analysis.mismatch.empty()) {
					analysis.send_first = true;
					analysis.mismatch = AsBytes(analysis.match->GetPartialMatchString(analysis.a_match));

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

				assert(analysis.state == State::INSERT);
				/* blocking */
				return data - data0;
			}

			assert(analysis.state == State::NONE);

			break;
		}
	} while (p < end || analysis.state == State::INSERT);

	size_t chunk_length;
	if (first != nullptr)
		/* we have found a partial match which we discard now, instead
		   we will write the chunk right before this match */
		chunk_length = first - data;
	else if (analysis.state == State::MATCH || analysis.state == State::INSERT)
		chunk_length = 0;
	else
		/* there was no match (maybe a partial match which mismatched
		   at a later stage): pass everything */
		chunk_length = end - data;

	if (chunk_length > 0) {
		/* write chunk */

		const size_t nbytes = ForwardSourceData(data0, {data, chunk_length},
							destructed);
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
	if (!analysis.mismatch.empty() && FeedMismatch())
		return 0;

	return Feed(src);
}

void
SubstIstream::OnEof() noexcept
{
	assert(input.IsDefined());

	input.Clear();

	switch (analysis.state) {
	case State::NONE:
		break;

	case State::MATCH:
		/* note: not resetting analysis.state here because at
		   this point, nobody will ever use this variable
		   anymore */

		/* we're in the middle of a match, technically making this a
		   mismatch because we reach end of file before end of
		   match */
		if (analysis.mismatch.empty()) {
			analysis.mismatch = AsBytes(analysis.match->GetPartialMatchString(analysis.a_match));
			if (!WriteMismatch())
				DestroyEof();
			return;
		}
		break;

	case State::INSERT:
		if (auto nbytes = TryWriteB(); nbytes > 0)
			return;
		break;
	}

	/* we can't be in an existing "mismatch" if our input has
	   reported EOF */
	assert(analysis.mismatch.empty());

	if (analysis.state == State::NONE)
		DestroyEof();
}

void
SubstIstream::OnError(std::exception_ptr &&ep) noexcept
{
	assert(input.IsDefined());

	input.Clear();
	DestroyError(std::move(ep));
}

/*
 * istream implementation
 *
 */

void
SubstIstream::_Read() noexcept
{
	if (!analysis.mismatch.empty()) {
		if (input.IsDefined()) {
			if (FeedMismatch() || !input.IsDefined())
				return;
		} else {
			if (!WriteMismatch())
				DestroyEof();
			return;
		}
	} else {
		assert(input.IsDefined());
	}

	switch (analysis.state) {
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
			 !had_output && analysis.state != State::INSERT);

		return;
	}

	case State::INSERT:
		nbytes = TryWriteB();
		if (nbytes > 0)
			return;
		break;
	}

	if (analysis.state == State::NONE && !input.IsDefined() &&
	    analysis.mismatch.empty())
		DestroyEof();
}

void
SubstIstream::_FillBucketList(IstreamBucketList &list)
{
	if (!analysis.mismatch.empty()) {
		if (input.IsDefined()) {
			// FeedMismatch()

			if (analysis.send_first)
				list.Push(analysis.mismatch.first(1));

			// TODO: re-parse the rest of the mismatch buffer
			list.SetMore();
			list.EnableFallback(); // TODO eliminate
			UpdateBucketAvailable(list);
			return;
		} else {
			// WriteMismatch()
			list.Push(analysis.mismatch);
			UpdateBucketAvailable(list);
			return;
		}
	} else {
		assert(input.IsDefined());
	}

	switch (analysis.state) {
	case State::NONE: {
		IstreamBucketList tmp;
		FillBucketListFromInput(tmp);

		if (tmp.HasMore()) {
			list.CopyMoreFlagsFrom(tmp);
		}

		for (const auto &bucket : tmp) {
			if (!bucket.IsBuffer()) {
				list.SetMore();
				list.EnableFallback(); // TODO eliminate
				UpdateBucketAvailable(list);
				return;
			}

			auto s = ToStringView(bucket.GetBuffer());

			const char *first = FindFirstChar(s);
			if (first != nullptr) {
				s = Partition(s, first).first;
				if (!s.empty())
					list.Push(AsBytes(s));
				list.SetMore();
				list.EnableFallback(); // TOOD eliminate
				UpdateBucketAvailable(list);
				return;
			}

			list.Push(AsBytes(s));
		}

		UpdateBucketAvailable(list);
		return;
	}

	case State::MATCH:
		// TODO: read from input
		list.SetMore();
		list.EnableFallback(); // TODO eliminate
		UpdateBucketAvailable(list);
		return;

	case State::INSERT: {
		// TryWriteB
		assert(analysis.state == State::INSERT);
		assert(analysis.a_match > 0);
		assert(analysis.match != nullptr);
		assert(analysis.match->IsLeaf());
		assert(analysis.a_match == strlen(analysis.match->leaf.a));

		list.Push(analysis.GetB());
		list.SetMore();
		list.EnableFallback(); // TODO eliminate

		// TODO: read more
		UpdateBucketAvailable(list);
		return;
	}
	}
}

Istream::ConsumeBucketResult
SubstIstream::_ConsumeBucketList(size_t nbytes) noexcept
{
	assert(nbytes > 0);

	// TODO return eof flag?

	if (!analysis.mismatch.empty()) {
		if (input.IsDefined()) {
			// FeedMismatch()

			if (analysis.send_first) {
				analysis.send_first = false;
				return {BucketConsumed(1), false};
			}

			return {0, false};
		} else {
			// WriteMismatch()
			nbytes = analysis.ClampConsumeMismatch(nbytes);
			return {BucketConsumed(nbytes), false};
		}
	} else {
		assert(input.IsDefined());
	}

	switch (analysis.state) {
	case State::NONE:
		return BucketConsumed(input.ConsumeBucketList(nbytes));

	case State::MATCH:
		return {0, false};

	case State::INSERT: {
		// TryWriteB
		assert(analysis.state == State::INSERT);
		assert(analysis.a_match > 0);
		assert(analysis.match != nullptr);
		assert(analysis.match->IsLeaf());
		assert(analysis.a_match == strlen(analysis.match->leaf.a));

		const size_t length = analysis.match->leaf.b_length - analysis.b_sent;
		assert(length > 0);

		size_t consumed = std::min(nbytes, length);

		/* note progress */
		analysis.b_sent += consumed;

		/* finished sending substitution? */
		if (consumed == length)
			analysis.state = State::NONE;
		return {BucketConsumed(consumed), false};
	}
	}

	std::unreachable();
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
