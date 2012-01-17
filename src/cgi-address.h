/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CGI_ADDRESS_H
#define BENG_PROXY_CGI_ADDRESS_H

#include "jail.h"
#include "address-list.h"

#include <stdbool.h>

struct pool;

/**
 * The address of a CGI/FastCGI/WAS request.
 */
struct cgi_address {
    const char *path;

    const char *args[32];
    unsigned num_args;

    struct jail_params jail;

    const char *interpreter;
    const char *action;

    const char *uri;
    const char *script_name, *path_info, *query_string;
    const char *document_root;

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

void
cgi_address_copy(struct pool *pool, struct cgi_address *dest,
                 const struct cgi_address *src, bool have_address_list);

char *
cgi_address_auto_base(struct pool *pool, const struct cgi_address *address,
                      const char *uri);

bool
cgi_address_save_base(struct pool *pool, struct cgi_address *dest,
                      const struct cgi_address *src, const char *suffix,
                      bool have_address_list);

void
cgi_address_load_base(struct pool *pool, struct cgi_address *dest,
                      const struct cgi_address *src, const char *suffix,
                      bool have_address_list);

/**
 * @return dest on success, src if no change is needed, NULL on error
 */
const struct cgi_address *
cgi_address_apply(struct pool *pool, struct cgi_address *dest,
                  const struct cgi_address *src,
                  const char *relative, size_t relative_length,
                  bool have_address_list);

#endif
