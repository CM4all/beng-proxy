#include "resource_address.hxx"
#include "file_address.h"
#include "cgi_address.hxx"
#include "pool.h"

#include <assert.h>
#include <string.h>

static void
test_auto_base(struct pool *pool)
{
    static const struct cgi_address cgi0 = {
        .path = "/usr/lib/cgi-bin/foo.pl",
        .path_info = "/",
    };
    static const struct resource_address ra0 = {
        .type = RESOURCE_ADDRESS_CGI,
        .u = {
            .cgi = &cgi0,
        },
    };

    assert(resource_address_auto_base(pool, &ra0, "/") == NULL);
    assert(resource_address_auto_base(pool, &ra0, "/foo") == NULL);

    static const struct cgi_address cgi1 = {
        .path = "/usr/lib/cgi-bin/foo.pl",
        .path_info = "foo/bar",
    };
    static const struct resource_address ra1 = {
        .type = RESOURCE_ADDRESS_CGI,
        .u = {
            .cgi = &cgi1,
        },
    };

    assert(resource_address_auto_base(pool, &ra1, "/") == NULL);
    assert(resource_address_auto_base(pool, &ra1, "/foo/bar") == NULL);

    static const struct cgi_address cgi2 = {
        .path = "/usr/lib/cgi-bin/foo.pl",
        .path_info = "/bar/baz",
    };
    static const struct resource_address ra2 = {
        .type = RESOURCE_ADDRESS_CGI,
        .u = {
            .cgi = &cgi2,
        },
    };

    assert(resource_address_auto_base(pool, &ra2, "/") == NULL);
    assert(resource_address_auto_base(pool, &ra2, "/foobar/baz") == NULL);

    char *a = resource_address_auto_base(pool, &ra2, "/foo/bar/baz");
    assert(a != NULL);
    assert(strcmp(a, "/foo/") == 0);
}

/*
 * main
 *
 */

int main(int argc, char **argv) {
    struct pool *pool;
    static const struct file_address file1 = {
        .path = "/var/www/foo/bar.html",
    };
    static const struct resource_address ra1 = {
        .type = RESOURCE_ADDRESS_LOCAL,
        .u = {
            .file = &file1,
        },
    };

    static const struct file_address file2 = {
        .path = "/var/www/foo/space .txt",
    };
    static const struct resource_address ra2 = {
        .type = RESOURCE_ADDRESS_LOCAL,
        .u = {
            .file = &file2,
        },
    };

    static const struct cgi_address cgi3 = {
        .path = "/usr/lib/cgi-bin/foo.pl",
        .uri = "/foo/bar/baz",
        .path_info = "/bar/baz",
    };
    static const struct resource_address ra3 = {
        .type = RESOURCE_ADDRESS_CGI,
        .u = {
            .cgi = &cgi3,
        },
    };
    struct resource_address *a, *b, dest, dest2;

    (void)argc;
    (void)argv;

    pool = pool_new_libc(NULL, "root");

    a = resource_address_save_base(pool, &dest2, &ra1, "bar.html");
    assert(a != NULL);
    assert(a->type == RESOURCE_ADDRESS_LOCAL);
    assert(strcmp(a->u.file->path, "/var/www/foo/") == 0);

    b = resource_address_load_base(pool, &dest, a, "index.html");
    assert(b != NULL);
    assert(b->type == RESOURCE_ADDRESS_LOCAL);
    assert(strcmp(b->u.file->path, "/var/www/foo/index.html") == 0);

    a = resource_address_save_base(pool, &dest2, &ra2, "space%20.txt");
    assert(a != NULL);
    assert(a->type == RESOURCE_ADDRESS_LOCAL);
    assert(strcmp(a->u.file->path, "/var/www/foo/") == 0);

    b = resource_address_load_base(pool, &dest, a, "index%2ehtml");
    assert(b != NULL);
    assert(b->type == RESOURCE_ADDRESS_LOCAL);
    assert(strcmp(b->u.file->path, "/var/www/foo/index.html") == 0);

    a = resource_address_save_base(pool, &dest2, &ra3, "bar/baz");
    assert(a != NULL);
    assert(a->type == RESOURCE_ADDRESS_CGI);
    assert(strcmp(a->u.cgi->path, ra3.u.cgi->path) == 0);
    assert(strcmp(a->u.cgi->path_info, "/") == 0);

    b = resource_address_load_base(pool, &dest, a, "");
    assert(b != NULL);
    assert(b->type == RESOURCE_ADDRESS_CGI);
    assert(strcmp(b->u.cgi->path, ra3.u.cgi->path) == 0);
    assert(strcmp(b->u.cgi->uri, "/foo/") == 0);
    assert(strcmp(b->u.cgi->path_info, "/") == 0);

    b = resource_address_load_base(pool, &dest, a, "xyz");
    assert(b != NULL);
    assert(b->type == RESOURCE_ADDRESS_CGI);
    assert(strcmp(b->u.cgi->path, ra3.u.cgi->path) == 0);
    assert(strcmp(b->u.cgi->uri, "/foo/xyz") == 0);
    assert(strcmp(b->u.cgi->path_info, "/xyz") == 0);

    a = resource_address_save_base(pool, &dest2, &ra3, "baz");
    assert(a != NULL);
    assert(a->type == RESOURCE_ADDRESS_CGI);
    assert(strcmp(a->u.cgi->path, ra3.u.cgi->path) == 0);
    assert(strcmp(a->u.cgi->uri, "/foo/bar/") == 0);
    assert(strcmp(a->u.cgi->path_info, "/bar/") == 0);

    b = resource_address_load_base(pool, &dest, a, "bar/");
    assert(b != NULL);
    assert(b->type == RESOURCE_ADDRESS_CGI);
    assert(strcmp(b->u.cgi->path, ra3.u.cgi->path) == 0);
    assert(strcmp(b->u.cgi->uri, "/foo/bar/bar/") == 0);
    assert(strcmp(b->u.cgi->path_info, "/bar/bar/") == 0);

    b = resource_address_load_base(pool, &dest, a, "bar/xyz");
    assert(b != NULL);
    assert(b->type == RESOURCE_ADDRESS_CGI);
    assert(strcmp(b->u.cgi->path, ra3.u.cgi->path) == 0);
    assert(strcmp(b->u.cgi->uri, "/foo/bar/bar/xyz") == 0);
    assert(strcmp(b->u.cgi->path_info, "/bar/bar/xyz") == 0);

    test_auto_base(pool);

    pool_unref(pool);
    pool_commit();

    pool_recycler_clear();
}
