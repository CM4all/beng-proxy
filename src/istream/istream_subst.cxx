/*
 * This istream filter substitutes a word with another string.
 *
 * Bug: the first character of the search word must not be present a
 * second time, because backtracking is not implemented.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_subst.hxx"
#include "istream_internal.hxx"
#include "istream_oo.hxx"
#include "istream_pointer.hxx"
#include "strref.h"
#include "pool.hxx"
#include "util/Cast.hxx"

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

struct SubstIstream {
    struct istream output;
    IstreamPointer input;
    bool had_input, had_output;

    bool send_first;

    SubstNode *root = nullptr;
    const SubstNode *match;
    struct strref mismatch;

    enum {
        /** searching the first matching character */
        STATE_NONE,

        /** the istream has been closed */
        STATE_CLOSED,

        /** at least the first character was found, checking for the
            rest */
        STATE_MATCH,

        /** inserting the substitution */
        STATE_INSERT,
    } state = STATE_NONE;
    size_t a_match, b_sent;

    SubstIstream(struct pool &p, struct istream &_input);

    bool Add(const char *a0, const char *b, size_t b_length);

    /** find the first occurence of a "first character" in the buffer */
    const char *FindFirstChar(const char *data, size_t length);

    /**
     * Write data from "b".
     *
     * @return the number of bytes remaining
     */
    size_t TryWriteB();

    bool FeedMismatch();
    bool WriteMismatch();

    /**
     * Forwards source data to the istream handler.
     *
     * @return (size_t)-1 when everything has been consumed, or the
     * correct return value for the data() callback.
     */
    size_t ForwardSourceData(const char *start, const char *p, size_t length);
    size_t ForwardSourceDataFinal(const char *start,
                                  const char *end, const char *p);

    size_t Feed(const void *data, size_t length);

    /* istream handler */

    size_t OnData(const void *data, size_t length);

    ssize_t OnDirect(gcc_unused FdType type, gcc_unused int fd,
                     gcc_unused size_t max_length) {
        gcc_unreachable();
    }

    void OnEof();
    void OnError(GError *error);
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

inline const char *
SubstIstream::FindFirstChar(const char *data, size_t length)
{
    SubstNode *node = root;
    const char *min = nullptr;

    while (node != nullptr) {
        assert(node->ch != 0);

        auto *p = (const char *)memchr(data, node->ch, length);
        if (p != nullptr && (min == nullptr || p < min)) {
            assert(node->equals != nullptr);
            match = node->equals;
            min = p;
        }

        node = subst_next_non_leaf_node(node, root);
    }

    return min;
}

/** find a character in the tree */
gcc_pure
static const SubstNode *
subst_find_char(const SubstNode *node, char ch)
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
subst_find_leaf(const SubstNode *node)
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
subst_find_any_leaf(const SubstNode *node)
{
    while (true) {
        assert(node != nullptr);

        if (node->ch == 0)
            return node;

        node = node->equals;
    }
}

size_t
SubstIstream::TryWriteB()
{
    assert(state == STATE_INSERT);
    assert(a_match > 0);
    assert(match != nullptr);
    assert(match->ch == 0);
    assert(a_match == strlen(match->leaf.a));

    const size_t length = match->leaf.b_length - b_sent;
    assert(length > 0);

    const size_t nbytes = istream_invoke_data(&output, match->leaf.b + b_sent,
                                              length);
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
SubstIstream::FeedMismatch()
{
    assert(state == STATE_NONE);
    assert(input.IsDefined());
    assert(!strref_is_empty(&mismatch));

    if (send_first) {
        const size_t nbytes = istream_invoke_data(&output, mismatch.data, 1);
        if (nbytes == 0)
            return true;

        ++mismatch.data;
        --mismatch.length;

        if (strref_is_empty(&mismatch))
            return false;

        send_first = false;
    }

    pool_ref(output.pool);
    const size_t nbytes = Feed(mismatch.data, mismatch.length);
    pool_unref(output.pool);
    if (nbytes == 0)
        return true;

    assert(nbytes <= mismatch.length);

    mismatch.data += nbytes;
    mismatch.length -= nbytes;

    return !strref_is_empty(&mismatch);
}

bool
SubstIstream::WriteMismatch()
{
    assert(!input.IsDefined() || state == STATE_NONE);
    assert(!strref_is_empty(&mismatch));

    size_t nbytes = istream_invoke_data(&output,
                                        mismatch.data,
                                        mismatch.length);
    if (nbytes == 0)
        return true;

    assert(nbytes <= mismatch.length);

    mismatch.data += nbytes;
    mismatch.length -= nbytes;

    if (!strref_is_empty(&mismatch))
        return true;

    if (!input.IsDefined()) {
        istream_deinit_eof(&output);
        return true;
    }

    return false;
}

size_t
SubstIstream::ForwardSourceData(const char *start,
                                const char *p, size_t length)
{
    size_t nbytes = istream_invoke_data(&output, p, length);
    if (nbytes == 0 && state == STATE_CLOSED)
        /* stream has been closed - we must return 0 */
        return 0;

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
                                     const char *end, const char *p)
{
    size_t nbytes = istream_invoke_data(&output, p, end - p);
    if (nbytes > 0 || state != STATE_CLOSED) {
        had_output = true;
        nbytes += (p - start);
    }

    return nbytes;
}

size_t
SubstIstream::Feed(const void *_data, size_t length)
{
    assert(input.IsDefined());

    const char *const data0 = (const char *)_data, *data = data0, *p = data0,
        *const end = p + length, *first = nullptr;
    const SubstNode *node;

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

        case STATE_CLOSED:
            assert(0);

        case STATE_MATCH:
            /* now see if the rest matches; note that max_compare may be
               0, but that isn't a problem */

            node = subst_find_char(match, *p);
            if (node != nullptr) {
                /* next character matches */

                ++a_match;
                ++p;
                match = node;

                node = subst_find_leaf(node);
                if (node != nullptr) {
                    /* full match */

                    match = node;

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

                    if (node->leaf.b_length > 0) {
                        state = STATE_INSERT;
                        b_sent = 0;
                    } else {
                        state = STATE_NONE;
                    }
                }
            } else {
                /* mismatch. reset match indicator and find new one */

                if (first != nullptr && (first > data ||
                                      !strref_is_empty(&mismatch))) {
                    /* write the data chunk before the (mis-)match */

                    had_output = true;

                    size_t chunk_length = first - data;
                    if (!strref_is_empty(&mismatch))
                        ++chunk_length;

                    const size_t nbytes =
                        ForwardSourceData(data0, data, chunk_length);
                    if (nbytes != (size_t)-1)
                        return nbytes;
                } else {
                    /* when re-parsing a mismatch, "first" must not be
                       nullptr because we entered this function with
                       state=STATE_NONE */
                    assert(strref_is_empty(&mismatch));
                }

                /* move data pointer */

                data = p;
                first = nullptr;

                /* switch state */

                /* seek any leaf to get a valid match->leaf.a which we
                   can use to re-insert the partial match into the
                   stream */

                state = STATE_NONE;

                if (strref_is_empty(&mismatch)) {
                    send_first = true;

                    node = subst_find_any_leaf(match);
                    assert(node != nullptr);
                    assert(node->ch == 0);
                    strref_set(&mismatch, node->leaf.a, a_match);

                    if (FeedMismatch())
                        return state == STATE_CLOSED ? 0 : data - data0;
                }
            }

            break;

        case STATE_INSERT:
            /* there is a previous full match, copy data from b */

            const size_t nbytes = TryWriteB();
            if (nbytes > 0) {
                if (state == STATE_CLOSED)
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
SubstIstream::OnData(const void *data, size_t length)
{
    if (!strref_is_empty(&mismatch) && FeedMismatch())
        return 0;

    pool_ref(output.pool);
    size_t nbytes = Feed(data, length);
    pool_unref(output.pool);

    return nbytes;
}

inline void
SubstIstream::OnEof()
{
    assert(input.IsDefined());

    input.Clear();

    switch (state) {
        size_t nbytes;

    case STATE_NONE:
        break;

    case STATE_CLOSED:
        assert(0);

    case STATE_MATCH:
        /* we're in the middle of a match, technically making this a
           mismatch because we reach end of file before end of
           match */
        if (strref_is_empty(&mismatch)) {
            const SubstNode *node = subst_find_any_leaf(match);
            assert(node != nullptr);
            assert(node->ch == 0);

            strref_set(&mismatch, node->leaf.a, a_match);
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

    if (state == STATE_NONE) {
        state = STATE_CLOSED;
        istream_deinit_eof(&output);
    }
}

inline void
SubstIstream::OnError(GError *error)
{
    assert(input.IsDefined());

    state = STATE_CLOSED;

    input.Clear();
    istream_deinit_abort(&output, error);
}

/*
 * istream implementation
 *
 */

static inline SubstIstream *
istream_to_subst(struct istream *istream)
{
    return &ContainerCast2(*istream, &SubstIstream::output);
}

static void
istream_subst_read(struct istream *istream)
{
    SubstIstream *subst = istream_to_subst(istream);

    if (!strref_is_empty(&subst->mismatch)) {
        bool ret = subst->input.IsDefined()
            ? subst->FeedMismatch()
            : subst->WriteMismatch();

        if (ret || !subst->input.IsDefined())
            return;
    } else {
        assert(subst->input.IsDefined());
    }

    switch (subst->state) {
        size_t nbytes;

    case SubstIstream::STATE_NONE:
    case SubstIstream::STATE_MATCH:
        assert(subst->input.IsDefined());

        subst->had_output = false;

        pool_ref(subst->output.pool);

        do {
            subst->had_input = false;
            subst->input.Read();
        } while (subst->input.IsDefined() && subst->had_input &&
                 !subst->had_output && subst->state != SubstIstream::STATE_INSERT);

        pool_unref(subst->output.pool);

        return;

    case SubstIstream::STATE_CLOSED:
        assert(0);

    case SubstIstream::STATE_INSERT:
        nbytes = subst->TryWriteB();
        if (nbytes > 0)
            return;
        break;
    }

    if (subst->state == SubstIstream::STATE_NONE && !subst->input.IsDefined()) {
        subst->state = SubstIstream::STATE_CLOSED;
        istream_deinit_eof(&subst->output);
    }
}

static void
istream_subst_close(struct istream *istream)
{
    SubstIstream *subst = istream_to_subst(istream);

    subst->state = SubstIstream::STATE_CLOSED;

    if (subst->input.IsDefined())
        subst->input.ClearAndClose();

    istream_deinit(&subst->output);
}

static const struct istream_class istream_subst = {
    .read = istream_subst_read,
    .close = istream_subst_close,
};


/*
 * constructor
 *
 */

inline
SubstIstream::SubstIstream(struct pool &p, struct istream &_input)
    :input(_input, MakeIstreamHandler<SubstIstream>::handler, this)
{
    istream_init(&output, &istream_subst, &p);

    strref_clear(&mismatch);
}

struct istream *
istream_subst_new(struct pool *pool, struct istream *input)
{
    assert(input != nullptr);
    assert(!istream_has_handler(input));

    auto subst = NewFromPool<SubstIstream>(*pool, *pool, *input);
    return &subst->output;
}

inline bool
SubstIstream::Add(const char *a0, const char *b, size_t b_length)
{
    SubstNode *parent = nullptr;
    const char *a = a0;

    assert(a0 != nullptr);
    assert(*a0 != 0);
    assert(b_length == 0 || b != nullptr);

    auto **pp = &root;
    do {
        auto *p = *pp;
        if (p == nullptr) {
            /* create new tree node */

            p = (SubstNode *)p_malloc(output.pool,
                                      sizeof(*p) - sizeof(p->leaf));
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
        p_malloc(output.pool, sizeof(*p) + b_length - sizeof(p->leaf.b));
    p->parent = parent;
    p->left = nullptr;
    p->right = nullptr;
    p->equals = nullptr;
    p->ch = 0;
    p->leaf.a = a0;
    p->leaf.b_length = b_length;
    memcpy(p->leaf.b, b, b_length);

    *pp = p;

    return true;
}

bool
istream_subst_add(struct istream *istream, const char *a, const char *b)
{
    SubstIstream *subst = istream_to_subst(istream);
    return subst->Add(a, b, b == nullptr ? 0 : strlen(b));
}
