/*
 * Copyright 2007-2017 Content Management AG
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

#include "istream_subst.hxx"
#include "FacadeIstream.hxx"
#include "UnusedPtr.hxx"
#include "pool/pool.hxx"
#include "util/DestructObserver.hxx"
#include "util/StringView.hxx"

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
    } leaf;
};

class SubstIstream final : public FacadeIstream, DestructAnchor {
    bool had_input, had_output;

    bool send_first;

    SubstTree tree;

    const SubstNode *match;
    StringView mismatch = nullptr;

    enum {
        /** searching the first matching character */
        STATE_NONE,

        /** at least the first character was found, checking for the
            rest */
        STATE_MATCH,

        /** inserting the substitution */
        STATE_INSERT,
    } state = STATE_NONE;
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

    size_t Feed(const void *data, size_t length) noexcept;

public:
    /* virtual methods from class Istream */

    void _Read() noexcept override;
    void _Close() noexcept override;

    /* istream handler */

    size_t OnData(const void *data, size_t length) noexcept override;

    void OnEof() noexcept override;
    void OnError(std::exception_ptr ep) noexcept override;
};

/*
 * helper functions
 *
 */

/** iterates over the current depth */
static SubstNode *
subst_next_non_leaf_node(SubstNode *node, SubstNode *root)
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
SubstTree::FindFirstChar(const char *data, size_t length) noexcept
{
    SubstNode *n = root;
    const SubstNode *match = nullptr;
    const char *min = nullptr;

    while (n != nullptr) {
        assert(n->ch != 0);

        auto *p = (const char *)memchr(data, n->ch, length);
        if (p != nullptr && (min == nullptr || p < min)) {
            assert(n->equals != nullptr);
            match = n->equals;
            min = p;
        }

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

/** find a character in the tree */
gcc_pure
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
gcc_pure
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

/** find any leaf which begins with the current partial match, used to
    find a buffer which is partially re-inserted into the data
    stream */
gcc_pure
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

size_t
SubstIstream::TryWriteB() noexcept
{
    assert(state == STATE_INSERT);
    assert(a_match > 0);
    assert(match != nullptr);
    assert(match->ch == 0);
    assert(a_match == strlen(match->leaf.a));

    const size_t length = match->leaf.b_length - b_sent;
    assert(length > 0);

    const size_t nbytes = InvokeData(match->leaf.b + b_sent, length);
    assert(nbytes <= length);
    if (nbytes > 0) {
        /* note progress */
        b_sent += nbytes;

        /* finished sending substitution? */
        if (nbytes == length)
            state = STATE_NONE;
    }

    return length - nbytes;
}

bool
SubstIstream::FeedMismatch() noexcept
{
    assert(state == STATE_NONE);
    assert(input.IsDefined());
    assert(!mismatch.empty());

    if (send_first) {
        const size_t nbytes = InvokeData(mismatch.data, 1);
        if (nbytes == 0)
            return true;

        mismatch.skip_front(1);

        if (mismatch.empty())
            return false;

        send_first = false;
    }

    const size_t nbytes = Feed(mismatch.data, mismatch.size);
    if (nbytes == 0)
        return true;

    assert(nbytes <= mismatch.size);

    mismatch.skip_front(nbytes);

    return !mismatch.empty();
}

bool
SubstIstream::WriteMismatch() noexcept
{
    assert(!input.IsDefined() || state == STATE_NONE);
    assert(!mismatch.empty());

    size_t nbytes = InvokeData(mismatch.data, mismatch.size);
    if (nbytes == 0)
        return true;

    assert(nbytes <= mismatch.size);

    mismatch.skip_front(nbytes);

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

    size_t nbytes = InvokeData(p, length);
    if (destructed) {
        /* stream has been closed - we must return 0 */
        assert(nbytes == 0);
        return 0;
    }

    had_output = true;

    if (nbytes < length) {
        /* blocking */
        state = STATE_NONE;
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

    size_t nbytes = InvokeData(p, end - p);
    if (nbytes > 0 || !destructed) {
        had_output = true;
        nbytes += (p - start);
    }

    return nbytes;
}

size_t
SubstIstream::Feed(const void *_data, size_t length) noexcept
{
    assert(input.IsDefined());

    const DestructObserver destructed(*this);

    const char *const data0 = (const char *)_data, *data = data0, *p = data0,
        *const end = p + length, *first = nullptr;
    const SubstNode *n;

    had_input = true;

    /* find new match */

    do {
        assert(data >= data0);
        assert(p >= data);
        assert(p <= end);

        switch (state) {
        case STATE_NONE:
            /* find matching first char */

            assert(first == nullptr);

            first = FindFirstChar(p, end - p);
            if (first == nullptr)
                /* no match, try to write and return */
                return ForwardSourceDataFinal(data0, end, data);

            state = STATE_MATCH;
            a_match = 1;

            p = first + 1;

            /* XXX check if match is full */
            break;

        case STATE_MATCH:
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
                        state = STATE_INSERT;
                        b_sent = 0;
                    } else {
                        state = STATE_NONE;
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
                       state=STATE_NONE */
                    assert(mismatch.empty());
                }

                /* move data pointer */

                data = p;
                first = nullptr;

                /* switch state */

                /* seek any leaf to get a valid match->leaf.a which we
                   can use to re-insert the partial match into the
                   stream */

                state = STATE_NONE;

                if (mismatch.empty()) {
                    send_first = true;

                    n = subst_find_any_leaf(match);
                    assert(n != nullptr);
                    assert(n->ch == 0);
                    mismatch = {n->leaf.a, a_match};

                    if (FeedMismatch())
                        return destructed ? 0 : data - data0;
                }
            }

            break;

        case STATE_INSERT:
            /* there is a previous full match, copy data from b */

            const size_t nbytes = TryWriteB();
            if (nbytes > 0) {
                if (destructed)
                    return 0;

                assert(state == STATE_INSERT);
                /* blocking */
                return data - data0;
            }

            assert(state == STATE_NONE);

            break;
        }
    } while (p < end || state == STATE_INSERT);

    size_t chunk_length;
    if (first != nullptr)
        /* we have found a partial match which we discard now, instead
           we will write the chunk right before this match */
        chunk_length = first - data;
    else if (state == STATE_MATCH || state == STATE_INSERT)
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
SubstIstream::OnData(const void *data, size_t length) noexcept
{
    if (!mismatch.empty() && FeedMismatch())
        return 0;

    const ScopePoolRef ref(GetPool() TRACE_ARGS);
    return Feed(data, length);
}

void
SubstIstream::OnEof() noexcept
{
    assert(input.IsDefined());

    input.Clear();

    switch (state) {
        size_t nbytes;

    case STATE_NONE:
        break;

    case STATE_MATCH:
        /* we're in the middle of a match, technically making this a
           mismatch because we reach end of file before end of
           match */
        if (mismatch.empty()) {
            const SubstNode *n = subst_find_any_leaf(match);
            assert(n != nullptr);
            assert(n->ch == 0);

            mismatch = {n->leaf.a, a_match};
            WriteMismatch();
            return;
        }
        break;

    case STATE_INSERT:
        nbytes = TryWriteB();
        if (nbytes > 0)
            return;
        break;
    }

    if (state == STATE_NONE)
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

    case STATE_NONE:
    case STATE_MATCH: {
        assert(input.IsDefined());

        had_output = false;

        const DestructObserver destructed(*this);

        do {
            had_input = false;
            input.Read();
        } while (!destructed && input.IsDefined() && had_input &&
                 !had_output && state != STATE_INSERT);

        return;
    }

    case STATE_INSERT:
        nbytes = TryWriteB();
        if (nbytes > 0)
            return;
        break;
    }

    if (state == STATE_NONE && !input.IsDefined())
        DestroyEof();
}

void
SubstIstream::_Close() noexcept
{
    if (input.IsDefined())
        input.ClearAndClose();

    Destroy();
}

/*
 * constructor
 *
 */

UnusedIstreamPtr
istream_subst_new(struct pool *pool, UnusedIstreamPtr input,
                  SubstTree tree) noexcept
{
    return UnusedIstreamPtr(NewIstream<SubstIstream>(*pool, std::move(input),
                                                     std::move(tree)));
}

bool
SubstTree::Add(struct pool &pool, const char *a0, StringView b) noexcept
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
        p_malloc(&pool, sizeof(*p) + b.size - sizeof(p->leaf.b));
    p->parent = parent;
    p->left = nullptr;
    p->right = nullptr;
    p->equals = nullptr;
    p->ch = 0;
    p->leaf.a = a0;
    p->leaf.b_length = b.size;
    memcpy(p->leaf.b, b.data, b.size);

    *pp = p;

    return true;
}
