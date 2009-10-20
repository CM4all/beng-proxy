#include "resource-address.h"

#include <assert.h>
#include <string.h>

/*
 * main
 *
 */

int main(int argc, char **argv) {
    pool_t pool;
    static const struct resource_address ra1 = {
        .type = RESOURCE_ADDRESS_LOCAL,
        .u = {
            .local = {
                .path = "/var/www/foo/bar.html",
            },
        },
    };
    struct resource_address *a, *b;

    (void)argc;
    (void)argv;

    pool = pool_new_libc(NULL, "root");

    a = resource_address_save_base(pool, &ra1, "bar.html");
    assert(a != NULL);
    assert(a->type == RESOURCE_ADDRESS_LOCAL);
    assert(strcmp(a->u.local.path, "/var/www/foo/") == 0);

    b = resource_address_load_base(pool, a, "index.html");
    assert(b != NULL);
    assert(b->type == RESOURCE_ADDRESS_LOCAL);
    assert(strcmp(b->u.local.path, "/var/www/foo/index.html") == 0);

    b = resource_address_load_base(pool, a, "../hackme");
    assert(b == NULL);

    b = resource_address_load_base(pool, a, ".%2e/hackme");
    assert(b == NULL);

    b = resource_address_load_base(pool, a, "foo//bar");
    assert(b == NULL);

    b = resource_address_load_base(pool, a, "foo/./bar");
    assert(b == NULL);

    b = resource_address_load_base(pool, a, "foo/../bar");
    assert(b == NULL);

    b = resource_address_load_base(pool, a, "foo/%2e/bar");
    assert(b == NULL);

    b = resource_address_load_base(pool, a, "foo/.%2e/bar");
    assert(b == NULL);

    b = resource_address_load_base(pool, a, "foo/.%2e");
    assert(b == NULL);

    b = resource_address_load_base(pool, a, "f%00");
    assert(b == NULL);

    pool_unref(pool);
    pool_commit();

    pool_recycler_clear();
}
