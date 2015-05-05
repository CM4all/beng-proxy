/*
 * This istream filter substitutes a word with another string.
 *
 * Bug: the first character of the search word must not be present a
 * second time, because backtracking is not implemented.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_subst.hxx"
#include "istream-internal.h"
#include "strref.h"
#include "util/Cast.hxx"

#include <assert.h>
#include <string.h>

/* ternary search tree */
struct subst_node {
    struct subst_node *parent, *left, *right, *equals;
    char ch;

    struct {
        const char *a;
        size_t b_length;
        char b[1];
    } leaf;
};

struct istream_subst {
    struct istream output;
    struct istream *input;
    bool had_input, had_output;

    bool send_first;

    struct subst_node *root;
    const struct subst_node *match;
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
    } state;
    size_t a_match, b_sent;
};

/*
 * helper functions
 *
 */

/** iterates over the current depth */
static struct subst_node *
subst_next_non_leaf_node(struct subst_node *node, struct subst_node *root)
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

/** find the first occurence of a "first character" in the buffer */
static const char *
subst_find_first_char(struct istream_subst *subst,
                      const char *data, size_t length)
{
    struct subst_node *node = subst->root;
    const char *min = nullptr;

    while (node != nullptr) {
        assert(node->ch != 0);

        auto *p = (const char *)memchr(data, node->ch, length);
        if (p != nullptr && (min == nullptr || p < min)) {
            assert(node->equals != nullptr);
            subst->match = node->equals;
            min = p;
        }

        node = subst_next_non_leaf_node(node, subst->root);
    }

    return min;
}

/** find a character in the tree */
static const struct subst_node *
subst_find_char(const struct subst_node *node, char ch)
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
static const struct subst_node *
subst_find_leaf(const struct subst_node *node)
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
static const struct subst_node *
subst_find_any_leaf(const struct subst_node *node)
{
    while (true) {
        assert(node != nullptr);

        if (node->ch == 0)
            return node;

        node = node->equals;
    }
}

/**
 * write data from subst->b
 *
 * @return the number of bytes remaining
 */
static size_t
subst_try_write_b(struct istream_subst *subst)
{
    assert(subst->state == istream_subst::STATE_INSERT);
    assert(subst->a_match > 0);
    assert(subst->match != nullptr);
    assert(subst->match->ch == 0);
    assert(subst->a_match == strlen(subst->match->leaf.a));

    const size_t length = subst->match->leaf.b_length - subst->b_sent;
    assert(length > 0);

    const size_t nbytes = istream_invoke_data(&subst->output,
                                              subst->match->leaf.b + subst->b_sent,
                                              length);
    assert(nbytes <= length);
    if (nbytes > 0) {
        /* note progress */
        subst->b_sent += nbytes;

        /* finished sending substitution? */
        if (nbytes == length)
            subst->state = istream_subst::STATE_NONE;
    }

    return length - nbytes;
}

static size_t
subst_feed(struct istream_subst *subst, const void *_data, size_t length);

static bool
subst_feed_mismatch(struct istream_subst *subst)
{
    assert(subst->state == istream_subst::STATE_NONE);
    assert(subst->input != nullptr);
    assert(!strref_is_empty(&subst->mismatch));

    if (subst->send_first) {
        const size_t nbytes = istream_invoke_data(&subst->output,
                                                  subst->mismatch.data, 1);
        if (nbytes == 0)
            return true;

        ++subst->mismatch.data;
        --subst->mismatch.length;

        if (strref_is_empty(&subst->mismatch))
            return false;

        subst->send_first = false;
    }

    pool_ref(subst->output.pool);
    const size_t nbytes = subst_feed(subst, subst->mismatch.data,
                                     subst->mismatch.length);
    pool_unref(subst->output.pool);
    if (nbytes == 0)
        return true;

    assert(nbytes <= subst->mismatch.length);

    subst->mismatch.data += nbytes;
    subst->mismatch.length -= nbytes;

    return !strref_is_empty(&subst->mismatch);
}

static bool
subst_write_mismatch(struct istream_subst *subst)
{
    assert(subst->input == nullptr ||
           subst->state == istream_subst::STATE_NONE);
    assert(!strref_is_empty(&subst->mismatch));

    size_t nbytes = istream_invoke_data(&subst->output,
                                        subst->mismatch.data,
                                        subst->mismatch.length);
    if (nbytes == 0)
        return true;

    assert(nbytes <= subst->mismatch.length);

    subst->mismatch.data += nbytes;
    subst->mismatch.length -= nbytes;

    if (!strref_is_empty(&subst->mismatch))
        return true;

    if (subst->input == nullptr) {
        istream_deinit_eof(&subst->output);
        return true;
    }

    return false;
}

/**
 * Forwards source data to the istream handler.
 *
 * @return (size_t)-1 when everything has been consumed, or the
 * correct return value for the data() callback.
 */
static size_t
subst_invoke_data(struct istream_subst *subst, const char *start,
                  const char *p, size_t length)
{
    size_t nbytes = istream_invoke_data(&subst->output, p, length);
    if (nbytes == 0 && subst->state == istream_subst::STATE_CLOSED)
        /* stream has been closed - we must return 0 */
        return 0;

    subst->had_output = true;

    if (nbytes < length) {
        /* blocking */
        subst->state = istream_subst::STATE_NONE;
        return (p - start) + nbytes;
    } else
        /* everything has been consumed */
        return (size_t)-1;
}

static size_t
subst_invoke_data_final(struct istream_subst *subst, const char *start,
                        const char *end, const char *p)
{
    size_t nbytes = istream_invoke_data(&subst->output, p, end - p);
    if (nbytes > 0 || subst->state != istream_subst::STATE_CLOSED) {
        subst->had_output = true;
        nbytes += (p - start);
    }

    return nbytes;
}

static size_t
subst_feed(struct istream_subst *subst, const void *_data, size_t length)
{
    const char *const data0 = (const char *)_data, *data = data0, *p = data0,
        *const end = p + length, *first = nullptr;
    const struct subst_node *node;

    assert(subst->input != nullptr);

    subst->had_input = true;

    /* find new match */

    do {
        assert(data >= data0);
        assert(p >= data);
        assert(p <= end);

        switch (subst->state) {
        case istream_subst::STATE_NONE:
            /* find matching first char */

            assert(first == nullptr);

            first = subst_find_first_char(subst, p, end - p);
            if (first == nullptr)
                /* no match, try to write and return */
                return subst_invoke_data_final(subst, data0, end, data);

            subst->state = istream_subst::STATE_MATCH;
            subst->a_match = 1;

            p = first + 1;

            /* XXX check if match is full */
            break;

        case istream_subst::STATE_CLOSED:
            assert(0);

        case istream_subst::STATE_MATCH:
            /* now see if the rest matches; note that max_compare may be
               0, but that isn't a problem */

            node = subst_find_char(subst->match, *p);
            if (node != nullptr) {
                /* next character matches */

                ++subst->a_match;
                ++p;
                subst->match = node;

                node = subst_find_leaf(node);
                if (node != nullptr) {
                    /* full match */

                    subst->match = node;

                    if (first != nullptr && first > data) {
                        /* write the data chunk before the match */

                        subst->had_output = true;

                        const size_t chunk_length = first - data;
                        const size_t nbytes =
                            subst_invoke_data(subst, data0,
                                              data, chunk_length);
                        if (nbytes != (size_t)-1)
                            return nbytes;
                    }

                    /* move data pointer */

                    data = p;
                    first = nullptr;

                    /* switch state */

                    if (node->leaf.b_length > 0) {
                        subst->state = istream_subst::STATE_INSERT;
                        subst->b_sent = 0;
                    } else {
                        subst->state = istream_subst::STATE_NONE;
                    }
                }
            } else {
                /* mismatch. reset match indicator and find new one */

                if (first != nullptr && (first > data ||
                                      !strref_is_empty(&subst->mismatch))) {
                    /* write the data chunk before the (mis-)match */

                    subst->had_output = true;

                    size_t chunk_length = first - data;
                    if (!strref_is_empty(&subst->mismatch))
                        ++chunk_length;

                    const size_t nbytes = subst_invoke_data(subst, data0, data,
                                                            chunk_length);
                    if (nbytes != (size_t)-1)
                        return nbytes;
                } else {
                    /* when re-parsing a mismatch, "first" must not be
                       nullptr because we entered this function with
                       state=STATE_NONE */
                    assert(strref_is_empty(&subst->mismatch));
                }

                /* move data pointer */

                data = p;
                first = nullptr;

                /* switch state */

                /* seek any leaf to get a valid match->leaf.a which we
                   can use to re-insert the partial match into the
                   stream */

                subst->state = istream_subst::STATE_NONE;

                if (strref_is_empty(&subst->mismatch)) {
                    bool ret;

                    subst->send_first = true;

                    node = subst_find_any_leaf(subst->match);
                    assert(node != nullptr);
                    assert(node->ch == 0);
                    strref_set(&subst->mismatch, node->leaf.a, subst->a_match);

                    ret = subst_feed_mismatch(subst);
                    if (ret)
                        return subst->state == istream_subst::STATE_CLOSED ? 0 : data - data0;
                }
            }

            break;

        case istream_subst::STATE_INSERT:
            /* there is a previous full match, copy data from subst->b */

            const size_t nbytes = subst_try_write_b(subst);
            if (nbytes > 0) {
                if (subst->state == istream_subst::STATE_CLOSED)
                    return 0;

                assert(subst->state == istream_subst::STATE_INSERT);
                /* blocking */
                return data - data0;
            }

            assert(subst->state == istream_subst::STATE_NONE);

            break;
        }
    } while (p < end || subst->state == istream_subst::STATE_INSERT);

    size_t chunk_length;
    if (first != nullptr)
        /* we have found a partial match which we discard now, instead
           we will write the chunk right before this match */
        chunk_length = first - data;
    else if (subst->state == istream_subst::STATE_MATCH ||
             subst->state == istream_subst::STATE_INSERT)
        chunk_length = 0;
    else
        /* there was no match (maybe a partial match which mismatched
           at a later stage): pass everything */
        chunk_length = end - data;

    if (chunk_length > 0) {
        /* write chunk */

        subst->had_output = true;

        const size_t nbytes = subst_invoke_data(subst, data0, data,
                                                chunk_length);
        if (nbytes != (size_t)-1)
            return nbytes;
    }

    return p - data0;
}

/*
 * istream handler
 *
 */

static size_t
subst_input_data(const void *data, size_t length, void *ctx)
{
    auto *subst = (struct istream_subst *)ctx;

    if (!strref_is_empty(&subst->mismatch)) {
        bool ret = subst_feed_mismatch(subst);
        if (ret)
            return 0;
    }

    pool_ref(subst->output.pool);
    size_t nbytes = subst_feed(subst, data, length);
    pool_unref(subst->output.pool);

    return nbytes;
}

static void
subst_input_eof(void *ctx)
{
    auto *subst = (struct istream_subst *)ctx;

    assert(subst->input != nullptr);

    subst->input = nullptr;

    switch (subst->state) {
        size_t nbytes;

    case istream_subst::STATE_NONE:
        break;

    case istream_subst::STATE_CLOSED:
        assert(0);

    case istream_subst::STATE_MATCH:
        /* we're in the middle of a match, technically making this a
           mismatch because we reach end of file before end of
           match */
        if (strref_is_empty(&subst->mismatch)) {
            const struct subst_node *node = subst_find_any_leaf(subst->match);
            assert(node != nullptr);
            assert(node->ch == 0);

            strref_set(&subst->mismatch, node->leaf.a, subst->a_match);
            subst_write_mismatch(subst);
            return;
        }
        break;

    case istream_subst::STATE_INSERT:
        nbytes = subst_try_write_b(subst);
        if (nbytes > 0)
            return;
        break;
    }

    if (subst->state == istream_subst::STATE_NONE) {
        subst->state = istream_subst::STATE_CLOSED;
        istream_deinit_eof(&subst->output);
    }
}

static void
subst_input_abort(GError *error, void *ctx)
{
    auto *subst = (struct istream_subst *)ctx;

    subst->state = istream_subst::STATE_CLOSED;

    subst->input = nullptr;
    istream_deinit_abort(&subst->output, error);
}

static const struct istream_handler subst_input_handler = {
    .data = subst_input_data,
    .eof = subst_input_eof,
    .abort = subst_input_abort,
};


/*
 * istream implementation
 *
 */

static inline struct istream_subst *
istream_to_subst(struct istream *istream)
{
    return &ContainerCast2(*istream, &istream_subst::output);
}

static void
istream_subst_read(struct istream *istream)
{
    struct istream_subst *subst = istream_to_subst(istream);

    if (!strref_is_empty(&subst->mismatch)) {
        bool ret;

        if (subst->input == nullptr)
            ret = subst_write_mismatch(subst);
        else
            ret = subst_feed_mismatch(subst);

        if (ret || subst->input == nullptr)
            return;
    } else {
        assert(subst->input != nullptr);
    }

    switch (subst->state) {
        size_t nbytes;

    case istream_subst::STATE_NONE:
    case istream_subst::STATE_MATCH:
        assert(subst->input != nullptr);

        subst->had_output = false;

        pool_ref(subst->output.pool);

        do {
            subst->had_input = false;
            istream_read(subst->input);
        } while (subst->input != nullptr && subst->had_input &&
                 !subst->had_output && subst->state != istream_subst::STATE_INSERT);

        pool_unref(subst->output.pool);

        return;

    case istream_subst::STATE_CLOSED:
        assert(0);

    case istream_subst::STATE_INSERT:
        nbytes = subst_try_write_b(subst);
        if (nbytes > 0)
            return;
        break;
    }

    if (subst->state == istream_subst::STATE_NONE && subst->input == nullptr) {
        subst->state = istream_subst::STATE_CLOSED;
        istream_deinit_eof(&subst->output);
    }
}

static void
istream_subst_close(struct istream *istream)
{
    struct istream_subst *subst = istream_to_subst(istream);

    subst->state = istream_subst::STATE_CLOSED;

    if (subst->input != nullptr)
        istream_free_handler(&subst->input);

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

struct istream *
istream_subst_new(struct pool *pool, struct istream *input)
{
    struct istream_subst *subst = istream_new_macro(pool, subst);

    assert(input != nullptr);
    assert(!istream_has_handler(input));

    subst->root = nullptr;
    subst->state = istream_subst::STATE_NONE;
    strref_clear(&subst->mismatch);

    istream_assign_handler(&subst->input, input,
                           &subst_input_handler, subst,
                           0);

    return istream_struct_cast(&subst->output);
}

bool
istream_subst_add_n(struct istream *istream, const char *a0,
                    const char *b, size_t b_length)
{
    struct istream_subst *subst = istream_to_subst(istream);
    struct subst_node *parent = nullptr;
    const char *a = a0;

    assert(subst != nullptr);
    assert(a0 != nullptr);
    assert(*a0 != 0);
    assert(b_length == 0 || b != nullptr);

    auto **pp = &subst->root;
    do {
        auto *p = *pp;
        if (p == nullptr) {
            /* create new tree node */

            p = (struct subst_node *)p_malloc(subst->output.pool,
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

    struct subst_node *p = (struct subst_node *)
        p_malloc(subst->output.pool,
                 sizeof(*p) + b_length - sizeof(p->leaf.b));
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
    return istream_subst_add_n(istream, a, b, b == nullptr ? 0 : strlen(b));
}
