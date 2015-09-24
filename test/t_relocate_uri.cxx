#include "relocate_uri.hxx"
#include "http_address.hxx"
#include "pool.hxx"
#include "util/StringView.hxx"

#include <stdio.h>

static void
CheckString(const char *expected, const char *value)
{
    if (expected == nullptr) {
        if (value != nullptr) {
            fprintf(stderr, "Expected nullptr, got '%s'\n", value);
            abort();
        }
    } else {
        if (value == nullptr) {
            fprintf(stderr, "Expected '%s', got nullptr\n", expected);
            abort();
        } else if (strcmp(expected, value) != 0) {
            fprintf(stderr, "Expected '%s', got '%s'\n", expected, value);
            abort();
        }
    }
}

struct RelocateUriTest {
    const char *uri;
    const char *internal_host;
    const char *internal_path;
    const char *external_path;
    const char *base;
    const char *expected;
};

static constexpr RelocateUriTest relocate_uri_tests[] = {
    { "http://internal-host/int-base/c", "internal-host", "/int-base/request",
      "/ext-base/request", "/ext-base/",
      "https://external-host:80/ext-base/c" },

    { "//internal-host/int-base/c", "internal-host", "/int-base/request",
      "/ext-base/request", "/ext-base/",
      "https://external-host:80/ext-base/c" },

    { "/int-base/c", "i", "/int-base/request",
      "/ext-base/request", "/ext-base/",
      "https://external-host:80/ext-base/c" },

    /* fail: relative URI */
    { "c", "internal-host", "/int-base/request",
      "/ext-base/request", "/ext-base/",
      nullptr },

    /* fail: host mismatch */
    { "//host-mismatch/int-base/c", "internal-host", "/int-base/request",
      "/ext-base/request", "/ext-base/",
      nullptr },

    /* fail: internal base mismatch */
    { "http://internal-host/wrong-base/c", "internal-host", "/int-base/request",
      "/ext-base/request", "/ext-base/",
      nullptr },

    /* fail: external base mismatch */
    { "http://internal-host/int-base/c", "internal-host", "/int-base/request",
      "/wrong-base/request", "/ext-base/",
      nullptr },
};

static void
CheckRelocateUri(struct pool &pool, const char *uri,
                 const char *internal_host, StringView internal_path,
                 const char *external_scheme, const char *external_host,
                 StringView external_path, StringView base,
                 const char *expected)
{
    auto *relocated = RelocateUri(pool, uri, internal_host, internal_path,
                                  external_scheme, external_host,
                                  external_path, base);
    CheckString(expected, relocated);
}

static void
TestRelocateUri(struct pool &pool)
{
    for (const auto &i : relocate_uri_tests)
        CheckRelocateUri(pool, i.uri, i.internal_host, i.internal_path,
                         "https", "external-host:80",
                         i.external_path, i.base,
                         i.expected);
}

/*
 * the main test code
 *
 */

int main(gcc_unused int argc, gcc_unused char **argv)
{
    struct pool *root_pool, *pool;

    root_pool = pool_new_libc(nullptr, "root");

    pool = pool_new_libc(root_pool, "pool");
    TestRelocateUri(*pool);
    pool_unref(pool);
    pool_unref(root_pool);
    pool_commit();
    pool_recycler_clear();

    return EXIT_SUCCESS;
}
