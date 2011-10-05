#include "resource-address.h"

#include <assert.h>
#include <string.h>

/*
 * main
 *
 */

int main(int argc, char **argv) {
    struct pool *pool;
    static const struct resource_address ra1 = {
        .type = RESOURCE_ADDRESS_LOCAL,
        .u = {
            .local = {
                .path = "/var/www/foo/bar.html",
            },
        },
    };
    static const struct resource_address ra2 = {
        .type = RESOURCE_ADDRESS_LOCAL,
        .u = {
            .local = {
                .path = "/var/www/foo/space .txt",
            },
        },
    };
    static const struct resource_address ra3 = {
        .type = RESOURCE_ADDRESS_CGI,
        .u = {
            .cgi = {
                .path = "/usr/lib/cgi-bin/foo.pl",
                .path_info = "/bar/baz",
            },
        },
    };
    struct resource_address *a, *b, dest, dest2;

    (void)argc;
    (void)argv;

    pool = pool_new_libc(NULL, "root");

    a = resource_address_save_base(pool, &dest2, &ra1, "bar.html");
    assert(a != NULL);
    assert(a->type == RESOURCE_ADDRESS_LOCAL);
    assert(strcmp(a->u.local.path, "/var/www/foo/") == 0);

    b = resource_address_load_base(pool, &dest, a, "index.html");
    assert(b != NULL);
    assert(b->type == RESOURCE_ADDRESS_LOCAL);
    assert(strcmp(b->u.local.path, "/var/www/foo/index.html") == 0);

    b = resource_address_load_base(pool, &dest, a, "../hackme");
    assert(b == NULL);

    b = resource_address_load_base(pool, &dest, a, ".%2e/hackme");
    assert(b == NULL);

    b = resource_address_load_base(pool, &dest, a, "foo//bar");
    assert(b == NULL);

    b = resource_address_load_base(pool, &dest, a, "foo/./bar");
    assert(b == NULL);

    b = resource_address_load_base(pool, &dest, a, "foo/../bar");
    assert(b == NULL);

    b = resource_address_load_base(pool, &dest, a, "foo/%2e/bar");
    assert(b == NULL);

    b = resource_address_load_base(pool, &dest, a, "foo/.%2e/bar");
    assert(b == NULL);

    b = resource_address_load_base(pool, &dest, a, "foo/.%2e");
    assert(b == NULL);

    b = resource_address_load_base(pool, &dest, a, "f%00");
    assert(b == NULL);

    a = resource_address_save_base(pool, &dest2, &ra2, "space%20.txt");
    assert(a != NULL);
    assert(a->type == RESOURCE_ADDRESS_LOCAL);
    assert(strcmp(a->u.local.path, "/var/www/foo/") == 0);

    b = resource_address_load_base(pool, &dest, a, "index%2ehtml");
    assert(b != NULL);
    assert(b->type == RESOURCE_ADDRESS_LOCAL);
    assert(strcmp(b->u.local.path, "/var/www/foo/index.html") == 0);

    a = resource_address_save_base(pool, &dest2, &ra3, "bar/baz");
    assert(a != NULL);
    assert(a->type == RESOURCE_ADDRESS_CGI);
    assert(strcmp(a->u.cgi.path, ra3.u.cgi.path) == 0);
    assert(strcmp(a->u.cgi.path_info, "/") == 0);

    b = resource_address_load_base(pool, &dest, a, "");
    assert(b != NULL);
    assert(b->type == RESOURCE_ADDRESS_CGI);
    assert(strcmp(b->u.cgi.path, ra3.u.cgi.path) == 0);
    assert(strcmp(b->u.cgi.path_info, "/") == 0);

    b = resource_address_load_base(pool, &dest, a, "xyz");
    assert(b != NULL);
    assert(b->type == RESOURCE_ADDRESS_CGI);
    assert(strcmp(b->u.cgi.path, ra3.u.cgi.path) == 0);
    assert(strcmp(b->u.cgi.path_info, "/xyz") == 0);

    a = resource_address_save_base(pool, &dest2, &ra3, "baz");
    assert(a != NULL);
    assert(a->type == RESOURCE_ADDRESS_CGI);
    assert(strcmp(a->u.cgi.path, ra3.u.cgi.path) == 0);
    assert(strcmp(a->u.cgi.path_info, "/bar/") == 0);

    b = resource_address_load_base(pool, &dest, a, "bar/");
    assert(b != NULL);
    assert(b->type == RESOURCE_ADDRESS_CGI);
    assert(strcmp(b->u.cgi.path, ra3.u.cgi.path) == 0);
    assert(strcmp(b->u.cgi.path_info, "/bar/bar/") == 0);

    b = resource_address_load_base(pool, &dest, a, "bar/xyz");
    assert(b != NULL);
    assert(b->type == RESOURCE_ADDRESS_CGI);
    assert(strcmp(b->u.cgi.path, ra3.u.cgi.path) == 0);
    assert(strcmp(b->u.cgi.path_info, "/bar/bar/xyz") == 0);

    pool_unref(pool);
    pool_commit();

    pool_recycler_clear();
}
