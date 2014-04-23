/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CGI_ADDRESS_HXX
#define BENG_PROXY_CGI_ADDRESS_HXX

#include "child_options.h"
#include "address_list.h"
#include "param_array.hxx"

#include <inline/compiler.h>

#include <glib.h>

struct pool;

/**
 * The address of a CGI/FastCGI/WAS request.
 */
struct cgi_address {
    const char *path;

    /**
     * Command-line arguments.
     */
    struct param_array args;

    /**
     * Environment variables or other protocol-specific name/value
     * pairs.
     */
    struct param_array env;

    struct child_options options;

    const char *interpreter;
    const char *action;

    const char *uri;
    const char *script_name, *path_info, *query_string;
    const char *document_root;

    /**
     * The value of #TRANSLATE_EXPAND_PATH.  Only used by the
     * translation cache.
     */
    const char *expand_path;

    /**
     * The value of #TRANSLATE_EXPAND_PATH_INFO.  Only used by
     * the translation cache.
     */
    const char *expand_path_info;

    /**
     * An optional list of addresses to connect to.  If given
     * for a FastCGI resource, then beng-proxy connects to one
     * of the addresses instead of spawning a new child
     * process.
     */
    struct address_list address_list;
};

void
cgi_address_init(struct cgi_address *cgi, const char *path,
                 bool have_address_list);

struct cgi_address *
cgi_address_new(struct pool *pool, const char *path,
                bool have_address_list);

gcc_pure
const char *
cgi_address_uri(struct pool *pool, const struct cgi_address *cgi);

/**
 * Generates a string identifying the address.  This can be used as a
 * key in a hash table.  The string will be allocated by the specified
 * pool.
 */
gcc_pure
const char *
cgi_address_id(struct pool *pool, const struct cgi_address *address);

void
cgi_address_copy(struct pool *pool, struct cgi_address *dest,
                 const struct cgi_address *src, bool have_address_list);

struct cgi_address *
cgi_address_dup(struct pool *pool, const struct cgi_address *old,
                bool have_address_list);

char *
cgi_address_auto_base(struct pool *pool, const struct cgi_address *address,
                      const char *uri);

struct cgi_address *
cgi_address_save_base(struct pool *pool, const struct cgi_address *src,
                      const char *suffix, bool have_address_list);

struct cgi_address *
cgi_address_load_base(struct pool *pool, const struct cgi_address *src,
                      const char *suffix, bool have_address_list);

/**
 * @return a new object on success, src if no change is needed, NULL
 * on error
 */
const struct cgi_address *
cgi_address_apply(struct pool *pool, const struct cgi_address *src,
                  const char *relative, size_t relative_length,
                  bool have_address_list);

/**
 * Does this address need to be expanded with cgi_address_expand()?
 */
gcc_pure
static inline bool
cgi_address_is_expandable(const struct cgi_address *address)
{
    assert(address != NULL);

    return address->expand_path != NULL ||
        address->expand_path_info != NULL ||
        address->args.IsExpandable() ||
        address->env.IsExpandable();
}

bool
cgi_address_expand(struct pool *pool, struct cgi_address *address,
                   const GMatchInfo *match_info, GError **error_r);

#endif
