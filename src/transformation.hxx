/*
 * Transformations which can be applied to resources.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_TRANSFORMATION_HXX
#define BENG_TRANSFORMATION_HXX

#include "resource_address.hxx"
#include "glibfwd.hxx"

#include <inline/compiler.h>

#include <assert.h>

struct pool;

struct Transformation {
    Transformation *next;

    enum class Type {
        PROCESS,
        PROCESS_CSS,
        PROCESS_TEXT,
        FILTER,
    } type;

    union {
        struct {
            unsigned options;
        } processor;

        struct {
            unsigned options;
        } css_processor;

        struct resource_address filter;
    } u;

    /**
     * Returns true if the chain contains at least one "PROCESS"
     * transformation.
     */
    gcc_pure
    bool HasProcessor() const;

    /**
     * Returns true if the first "PROCESS" transformation in the chain (if
     * any) includes the "CONTAINER" processor option.
     */
    gcc_pure
    bool IsContainer() const;

    /**
     * Does this transformation need to be expanded with
     * transformation_expand()?
     */
    gcc_pure
    bool IsExpandable() const {
        return type == Type::FILTER &&
            resource_address_is_expandable(&u.filter);
    }

    /**
     * Does any transformation in the linked list need to be expanded with
     * transformation_expand()?
     */
    gcc_pure
    bool IsChainExpandable() const;

    gcc_malloc
    Transformation *Dup(struct pool *pool) const;

    gcc_malloc
    Transformation *DupChain(struct pool *pool) const;

    /**
     * Expand the strings in this transformation (not following the linked
     * lits) with the specified regex result.
     */
    bool Expand(struct pool *pool, const GMatchInfo *match_info,
                GError **error_r);

    /**
     * The same as transformation_expand(), but expand all transformations
     * in the linked list.
     */
    bool ExpandChain(struct pool *pool, const GMatchInfo *match_info,
                     GError **error_r);
};

#endif
