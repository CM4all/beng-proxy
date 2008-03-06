/*
 * This istream filter substitutes a word with another string.
 *
 * Bug: the first character of the search word must not be present a
 * second time, because backtracking is not implemented.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream-internal.h"
#include "strref.h"

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
    istream_t input;
    unsigned had_input:1, had_output:1;

    unsigned send_first:1;

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
    if (node->left != NULL && node->left->ch != 0)
        return node->left;

    /* if left does not exist, go right */
    if (node->right != NULL && node->right->ch != 0)
        return node->right;

    /* this subtree is finished, go up */
    while (1) {
        /* don't go above our root */
        if (node == root)
            return NULL;

        assert(node->parent != NULL);

        if (node->parent->left == node) {
            node = node->parent;

            /* only go to parent->right if we came from
               parent->left */
            if (node->right != NULL && node->right->ch != 0)
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
    const char *p, *min = NULL;

    while (node != NULL) {
        assert(node->ch != 0);

        p = memchr(data, node->ch, length);
        if (p != NULL && (min == NULL || p < min)) {
            assert(node->equals != NULL);
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
    assert(node != NULL);
    assert(ch != 0);

    do {
        if (node->ch == ch) {
            assert(node->equals != NULL);
            return node->equals;
        }

        if (ch < node->ch)
            node = node->left;
        else
            node = node->right;
    } while (node != NULL);

    return NULL;
}

/** find the leaf ending the current search word */
static const struct subst_node *
subst_find_leaf(const struct subst_node *node)
{
    assert(node != NULL);

    do {
        if (node->ch == 0)
            return node;

        if (0 < node->ch)
            node = node->left;
        else
            node = node->right;
    } while (node != NULL);

    return NULL;
}

/** find any leaf which begins with the current partial match, used to
    find a buffer which is partially re-inserted into the data
    stream */
static const struct subst_node *
subst_find_any_leaf(const struct subst_node *node)
{
    while (1) {
        assert(node != NULL);

        if (node->ch == 0)
            return node;

        node = node->equals;
    } 
}

/** write data from subst->b */
static size_t
subst_try_write_b(struct istream_subst *subst)
{
    size_t length, nbytes;

    assert(subst->state == STATE_INSERT);
    assert(subst->a_match > 0);
    assert(subst->match != NULL);
    assert(subst->match->ch == 0);
    assert(subst->a_match == strlen(subst->match->leaf.a));

    length = subst->match->leaf.b_length - subst->b_sent;
    if (length == 0) {
        subst->state = STATE_NONE;
        return 0;
    }

    nbytes = istream_invoke_data(&subst->output,
                                 subst->match->leaf.b + subst->b_sent,
                                 length);
    assert(nbytes <= length);
    if (nbytes > 0) {
        /* note progress */
        subst->b_sent += nbytes;

        /* finished sending substitution? */
        if (nbytes == length)
            subst->state = STATE_NONE;
    }

    return nbytes;
}

static size_t
subst_feed(struct istream_subst *subst, const void *_data, size_t length);

static int
subst_feed_mismatch(struct istream_subst *subst)
{
    size_t nbytes;

    assert(subst->state == STATE_NONE);
    assert(subst->input != NULL);
    assert(!strref_is_empty(&subst->mismatch));

    if (subst->send_first) {
        nbytes = istream_invoke_data(&subst->output, subst->mismatch.data, 1);
        if (nbytes == 0)
            return 1;

        ++subst->mismatch.data;
        --subst->mismatch.length;

        if (strref_is_empty(&subst->mismatch))
            return 0;

        subst->send_first = 0;
    }

    nbytes = subst_feed(subst, subst->mismatch.data,
                        subst->mismatch.length);
    if (nbytes == 0)
        return 1;

    assert(nbytes <= subst->mismatch.length);

    subst->mismatch.data += nbytes;
    subst->mismatch.length -= nbytes;

    return !strref_is_empty(&subst->mismatch);
}

static int
subst_write_mismatch(struct istream_subst *subst)
{
    size_t nbytes;

    assert(subst->input == NULL || subst->state == STATE_NONE);
    assert(!strref_is_empty(&subst->mismatch));

    nbytes = istream_invoke_data(&subst->output,
                                 subst->mismatch.data,
                                 subst->mismatch.length);
    if (nbytes == 0)
        return 1;

    assert(nbytes <= subst->mismatch.length);

    subst->mismatch.data += nbytes;
    subst->mismatch.length -= nbytes;

    if (!strref_is_empty(&subst->mismatch))
        return 1;

    if (subst->input == NULL) {
        istream_deinit_eof(&subst->output);
        return 1;
    }

    return 0;
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
    size_t nbytes;

    nbytes = istream_invoke_data(&subst->output, p, length);
    if (nbytes == 0 && subst->state == STATE_CLOSED)
        /* stream has been closed - we must return 0 */
        return 0;

    subst->had_output = 1;

    if (nbytes < length) {
        /* blocking */
        subst->state = STATE_NONE;
        return (p - start) + nbytes;
    } else
        /* everything has been consumed */
        return (size_t)-1;
}

static size_t
subst_invoke_data_final(struct istream_subst *subst, const char *start,
                        const char *end, const char *p)
{
    size_t nbytes;

    nbytes = istream_invoke_data(&subst->output, p, end - p);
    if (nbytes > 0 || subst->state != STATE_CLOSED) {
        subst->had_output = 1;
        nbytes += (p - start);
    }

    return nbytes;
}

static size_t
subst_feed(struct istream_subst *subst, const void *_data, size_t length)
{
    const char *data0 = _data, *data = data0, *p = data0, *end = p + length, *first = NULL;
    size_t chunk_length, nbytes;
    const struct subst_node *node;

    assert(subst->input != NULL);

    subst->had_input = 1;

    /* find new match */

    do {
        assert(data >= data0);
        assert(p >= data);
        assert(p <= end);

        switch (subst->state) {
        case STATE_NONE:
            /* find matching first char */

            assert(first == NULL);

            first = subst_find_first_char(subst, p, end - p);
            if (first == NULL)
                /* no match, try to write and return */
                return subst_invoke_data_final(subst, data0, end, data);

            subst->state = STATE_MATCH;
            subst->a_match = 1;

            p = first + 1;

            /* XXX check if match is full */
            break;

        case STATE_CLOSED:
            assert(0);

        case STATE_MATCH:
            /* now see if the rest matches; note that max_compare may be
               0, but that isn't a problem */

            node = subst_find_char(subst->match, *p);
            if (node != NULL) {
                /* next character matches */

                ++subst->a_match;
                ++p;
                subst->match = node;

                node = subst_find_leaf(node);
                if (node != NULL) {
                    /* full match */

                    subst->match = node;

                    if (first != NULL && first > data) {
                        /* write the data chunk before the match */

                        subst->had_output = 1;

                        chunk_length = first - data;
                        nbytes = subst_invoke_data(subst, data0, data, chunk_length);
                        if (nbytes != (size_t)-1)
                            return nbytes;
                    }

                    /* move data pointer */

                    data = p;
                    first = NULL;

                    /* switch state */

                    subst->state = STATE_INSERT;
                    subst->b_sent = 0;
                }
            } else {
                /* mismatch. reset match indicator and find new one */

                if (first != NULL && (first > data ||
                                      !strref_is_empty(&subst->mismatch))) {
                    /* write the data chunk before the (mis-)match */

                    subst->had_output = 1;

                    chunk_length = first - data;
                    if (!strref_is_empty(&subst->mismatch))
                        ++chunk_length;

                    nbytes = subst_invoke_data(subst, data0, data, chunk_length);
                    if (nbytes != (size_t)-1)
                        return nbytes;
                } else {
                    /* when re-parsing a mismatch, "first" must not be
                       NULL because we entered this function with
                       state=STATE_NONE */
                    assert(strref_is_empty(&subst->mismatch));
                }

                /* move data pointer */

                data = p;
                first = NULL;

                /* switch state */

                /* seek any leaf to get a valid match->leaf.a which we
                   can use to re-insert the partial match into the
                   stream */

                subst->state = STATE_NONE;

                if (strref_is_empty(&subst->mismatch)) {
                    int ret;

                    subst->send_first = 1;

                    node = subst_find_any_leaf(subst->match);
                    assert(node != NULL);
                    assert(node->ch == 0);
                    strref_set(&subst->mismatch, node->leaf.a, subst->a_match);

                    ret = subst_feed_mismatch(subst);
                    if (ret)
                        return subst->state == STATE_CLOSED ? 0 : end - data;
                }
            }

            break;

        case STATE_INSERT:
            /* there is a previous full match, copy data from subst->b */

            subst_try_write_b(subst);

            if (subst->state == STATE_CLOSED)
                return 0;

            if (subst->state == STATE_INSERT)
                /* blocking */
                return data - data0;

            break;
        }
    } while (p < end || subst->state == STATE_INSERT);

    if (first != NULL)
        /* we have found a partial match which we discard now, instead
           we will write the chunk right before this match */
        chunk_length = first - data;
    else if (subst->state == STATE_INSERT)
        chunk_length = 0;
    else
        /* there was no match (maybe a partial match which mismatched
           at a later stage): pass everything */
        chunk_length = end - data;

    if (chunk_length > 0) {
        /* write chunk */

        subst->had_output = 1;

        nbytes = subst_invoke_data(subst, data0, data, chunk_length);
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
subst_source_data(const void *data, size_t length, void *ctx)
{
    struct istream_subst *subst = ctx;

    if (!strref_is_empty(&subst->mismatch)) {
        int ret = subst_feed_mismatch(subst);
        if (ret)
            return 0;
    }

    return subst_feed(subst, data, length);
}

static void
subst_source_eof(void *ctx)
{
    struct istream_subst *subst = ctx;

    assert(subst->input != NULL);

    subst->input = NULL;

    switch (subst->state) {
    case STATE_NONE:
        break;

    case STATE_CLOSED:
        assert(0);

    case STATE_MATCH:
        /* we're in the middle of a match, technically making this a
           mismatch because we reach end of file before end of
           match */
        if (strref_is_empty(&subst->mismatch)) {
            const struct subst_node *node = subst_find_any_leaf(subst->match);
            assert(node != NULL);
            assert(node->ch == 0);

            strref_set(&subst->mismatch, node->leaf.a, subst->a_match);
            subst_write_mismatch(subst);
        }
        break;

    case STATE_INSERT:
        subst_try_write_b(subst);
        break;
    }

    if (subst->state == STATE_NONE) {
        subst->state = STATE_CLOSED;
        istream_deinit_eof(&subst->output);
    }
}

static void
subst_source_abort(void *ctx)
{
    struct istream_subst *subst = ctx;

    subst->state = STATE_CLOSED;

    subst->input = NULL;
    istream_deinit_abort(&subst->output);
}

static const struct istream_handler subst_source_handler = {
    .data = subst_source_data,
    .eof = subst_source_eof,
    .abort = subst_source_abort,
};


/*
 * istream implementation
 *
 */

static inline struct istream_subst *
istream_to_subst(istream_t istream)
{
    return (struct istream_subst *)(((char*)istream) - offsetof(struct istream_subst, output));
}

static void
istream_subst_read(istream_t istream)
{
    struct istream_subst *subst = istream_to_subst(istream);

    if (!strref_is_empty(&subst->mismatch)) {
        int ret;

        if (subst->input == NULL)
            ret = subst_write_mismatch(subst);
        else
            ret = subst_feed_mismatch(subst);

        if (ret || subst->input == NULL)
            return;
    } else {
        assert(subst->input != NULL);
    }

    switch (subst->state) {
    case STATE_NONE:
    case STATE_MATCH:
        assert(subst->input != NULL);

        subst->had_output = 0;

        do {
            subst->had_input = 0;
            istream_read(subst->input);
        } while (subst->input != NULL && subst->had_input &&
                 !subst->had_output);

        return;

    case STATE_CLOSED:
        assert(0);

    case STATE_INSERT:
        subst_try_write_b(subst);
        break;
    }

    if (subst->state == STATE_NONE && subst->input == NULL) {
        subst->state = STATE_CLOSED;
        istream_deinit_eof(&subst->output);
    }
}

static void
istream_subst_close(istream_t istream)
{
    struct istream_subst *subst = istream_to_subst(istream);

    subst->state = STATE_CLOSED;

    if (subst->input != NULL)
        istream_free_handler(&subst->input);

    istream_deinit_abort(&subst->output);
}

static const struct istream istream_subst = {
    .read = istream_subst_read,
    .close = istream_subst_close,
};


/*
 * constructor
 *
 */

istream_t
istream_subst_new(pool_t pool, istream_t input)
{
    struct istream_subst *subst = istream_new_macro(pool, subst);

    assert(input != NULL);
    assert(!istream_has_handler(input));

    subst->root = NULL;
    subst->state = STATE_NONE;
    strref_clear(&subst->mismatch);

    istream_assign_handler(&subst->input, input,
                           &subst_source_handler, subst,
                           0);

    return istream_struct_cast(&subst->output);
}

int
istream_subst_add(istream_t istream, const char *a0, const char *b)
{
    struct istream_subst *subst = istream_to_subst(istream);
    struct subst_node **pp, *p, *parent = NULL;
    const char *a = a0;
    size_t b_length;

    assert(subst != NULL);
    assert(a0 != NULL);
    assert(*a0 != 0);

    pp = &subst->root;
    do {
        p = *pp;
        if (p == NULL) {
            /* create new tree node */

            p = p_malloc(subst->output.pool, sizeof(*p) - sizeof(p->leaf));
            p->parent = parent;
            p->left = NULL;
            p->right = NULL;
            p->equals = NULL;
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
    if (*pp != NULL)
        return 0;

    b_length = b == NULL ? 0 : strlen(b);

    /* create new leaf node */

    p = p_malloc(subst->output.pool, sizeof(*p) + b_length - sizeof(p->leaf.b));
    p->parent = parent;
    p->left = NULL;
    p->right = NULL;
    p->equals = NULL;
    p->ch = 0;
    p->leaf.a = a0;
    p->leaf.b_length = b_length;
    memcpy(p->leaf.b, b, b_length);

    *pp = p;

    return 1;
}
