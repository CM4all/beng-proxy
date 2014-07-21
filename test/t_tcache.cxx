#include "tcache.hxx"
#include "tstock.hxx"
#include "translate_client.hxx"
#include "translate_request.hxx"
#include "translate_response.hxx"
#include "async.hxx"
#include "transformation.hxx"
#include "widget_view.hxx"
#include "beng-proxy/translation.h"
#include "http_address.hxx"
#include "file_address.hxx"
#include "cgi_address.hxx"
#include "pool.hxx"
#include "tpool.h"

#include <event.h>

#include <string.h>

const TranslateResponse *next_response, *expected_response;

void
tstock_translate(gcc_unused struct tstock *stock, struct pool *pool,
                 gcc_unused const TranslateRequest *request,
                 const TranslateHandler *handler, void *ctx,
                 gcc_unused struct async_operation_ref *async_ref)
{
    if (next_response != nullptr) {
        auto response = NewFromPool<TranslateResponse>(*pool);
        response->CopyFrom(pool, *next_response);
        response->max_age = next_response->max_age;
        resource_address_copy(*pool, &response->address,
                              &next_response->address);
        response->user = next_response->user;
        handler->response(response, ctx);
    } else
        handler->error(g_error_new(translate_quark(), 0, "Error"), ctx);
}

static bool
string_equals(const char *a, const char *b)
{
    if (a == nullptr || b == nullptr)
        return a == nullptr && b == nullptr;

    return strcmp(a, b) == 0;
}

template<typename T>
static bool
buffer_equals(ConstBuffer<T> a, ConstBuffer<T> b)
{
    if (a.IsNull() || b.IsNull())
        return a.IsNull() == b.IsNull();

    return a.size == b.size && memcmp(a.data, b.data, a.ToVoid().size) == 0;
}

static bool
http_address_equals(const struct http_address *a,
                    const struct http_address *b)
{
    return a->scheme == b->scheme &&
        string_equals(a->host_and_port, b->host_and_port) &&
        string_equals(a->path, b->path);
}

static bool
resource_address_equals(const struct resource_address *a,
                        const struct resource_address *b)
{
    assert(a != nullptr);
    assert(b != nullptr);

    if (a->type != b->type)
        return false;

    switch (a->type) {
    case RESOURCE_ADDRESS_NONE:
        return true;

    case RESOURCE_ADDRESS_LOCAL:
        assert(a->u.file->path != nullptr);
        assert(b->u.file->path != nullptr);

        return string_equals(a->u.file->path, b->u.file->path) &&
            string_equals(a->u.file->deflated, b->u.file->deflated) &&
            string_equals(a->u.file->gzipped, b->u.file->gzipped) &&
            string_equals(a->u.file->content_type, b->u.file->content_type) &&
            string_equals(a->u.file->delegate, b->u.file->delegate) &&
            string_equals(a->u.file->document_root, b->u.file->document_root);

    case RESOURCE_ADDRESS_CGI:
        assert(a->u.cgi->path != nullptr);
        assert(b->u.cgi->path != nullptr);

        return string_equals(a->u.cgi->path, b->u.cgi->path) &&
            string_equals(a->u.cgi->interpreter, b->u.cgi->interpreter) &&
            string_equals(a->u.cgi->action, b->u.cgi->action) &&
            string_equals(a->u.cgi->uri, b->u.cgi->uri) &&
            string_equals(a->u.cgi->script_name, b->u.cgi->script_name) &&
            string_equals(a->u.cgi->path_info, b->u.cgi->path_info) &&
            string_equals(a->u.cgi->query_string, b->u.cgi->query_string) &&
            string_equals(a->u.cgi->document_root, b->u.cgi->document_root);

    case RESOURCE_ADDRESS_HTTP:
    case RESOURCE_ADDRESS_AJP:
        assert(a->u.http != nullptr);
        assert(b->u.http != nullptr);

        return http_address_equals(a->u.http, b->u.http);

    default:
        /* not implemented */
        assert(false);
        return false;
    }
}

static bool
transformation_equals(const Transformation *a,
                      const Transformation *b)
{
    assert(a != nullptr);
    assert(b != nullptr);

    if (a->type != b->type)
        return false;

    switch (a->type) {
    case Transformation::Type::PROCESS:
        return a->u.processor.options == b->u.processor.options;

    case Transformation::Type::PROCESS_CSS:
        return a->u.css_processor.options == b->u.css_processor.options;

    case Transformation::Type::PROCESS_TEXT:
        return true;

    case Transformation::Type::FILTER:
        return resource_address_equals(&a->u.filter, &b->u.filter);
    }

    /* unreachable */
    assert(false);
    return false;
}

static bool
transformation_chain_equals(const Transformation *a,
                  const Transformation *b)
{
    while (a != nullptr && b != nullptr) {
        if (!transformation_equals(a, b))
            return false;

        a = a->next;
        b = b->next;
    }

    return a == nullptr && b == nullptr;
}

static bool
view_equals(const WidgetView *a, const WidgetView *b)
{
    assert(a != nullptr);
    assert(b != nullptr);

    return string_equals(a->name, b->name) &&
        resource_address_equals(&a->address, &b->address) &&
        a->filter_4xx == b->filter_4xx &&
        transformation_chain_equals(a->transformation, b->transformation);
}

static bool
view_chain_equals(const WidgetView *a, const WidgetView *b)
{
    while (a != nullptr && b != nullptr) {
        if (!view_equals(a, b))
            return false;

        a = a->next;
        b = b->next;
    }

    return a == nullptr && b == nullptr;
}

static bool
translate_response_equals(const TranslateResponse *a,
                          const TranslateResponse *b)
{
    if (a == nullptr || b == nullptr)
        return a == nullptr && b == nullptr;

    return string_equals(a->base, b->base) &&
        a->regex_tail == b->regex_tail &&
        string_equals(a->regex, b->regex) &&
        string_equals(a->inverse_regex, b->inverse_regex) &&
        a->easy_base == b->easy_base &&
        a->unsafe_base == b->unsafe_base &&
        string_equals(a->uri, b->uri) &&
        string_equals(a->test_path, b->test_path) &&
        buffer_equals(a->check, b->check) &&
        buffer_equals(a->want_full_uri, b->want_full_uri) &&
        resource_address_equals(&a->address, &b->address) &&
        view_chain_equals(a->views, b->views);
}

static void
my_translate_response(TranslateResponse *response,
                      gcc_unused void *ctx)
{
    assert(translate_response_equals(response, expected_response));
}

static void
my_translate_error(GError *error, gcc_unused void *ctx)
{
    assert(expected_response == nullptr);

    g_error_free(error);
}

static const TranslateHandler my_translate_handler = {
    .response = my_translate_response,
    .error = my_translate_error,
};

static void
test_basic(struct pool *pool, struct tcache *cache)
{
    static const TranslateRequest request1 = {
        .uri = "/",
    };
    static const TranslateRequest request2 = {
        .uri = "/foo/bar.html",
    };
    static const TranslateRequest request3 = {
        .uri = "/foo/index.html",
    };
    static const TranslateRequest request4 = {
        .uri = "/foo/",
    };
    static const TranslateRequest request5 = {
        .uri = "/foo",
    };

    static const struct file_address file1("/var/www/index.html");
    static const TranslateResponse response1 = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .file = &file1,
            },
        },
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
    };

    static const struct file_address file2("/srv/foo/bar.html");
    static const TranslateResponse response2 = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .file = &file2,
            },
        },
        .base = "/foo/",
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
    };

    static const struct file_address file3("/srv/foo/index.html");
    static const TranslateResponse response3 = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .file = &file3,
            },
        },
        .base = "/foo/",
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
    };

    static const struct file_address file4("/srv/foo/");
    static const TranslateResponse response4 = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .file = &file4,
            },
        },
        .base = "/foo/",
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
    };

    struct async_operation_ref async_ref;

    next_response = expected_response = &response1;
    translate_cache(pool, cache, &request1,
                    &my_translate_handler, nullptr, &async_ref);

    next_response = nullptr;
    translate_cache(pool, cache, &request1,
                    &my_translate_handler, nullptr, &async_ref);

    next_response = expected_response = &response2;
    translate_cache(pool, cache, &request2,
                    &my_translate_handler, nullptr, &async_ref);

    next_response = nullptr;
    expected_response = &response3;
    translate_cache(pool, cache, &request3,
                    &my_translate_handler, nullptr, &async_ref);

    expected_response = &response4;
    translate_cache(pool, cache, &request4,
                    &my_translate_handler, nullptr, &async_ref);

    expected_response = nullptr;
    translate_cache(pool, cache, &request5,
                    &my_translate_handler, nullptr, &async_ref);

    static const TranslateRequest request10 = {
        .uri = "/foo//bar",
    };
    static const struct file_address file10("/srv/foo//bar");
    static const TranslateResponse response10 = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .file = &file10,
            },
        },
        .base = "/foo/",
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
    };
    expected_response = &response10;
    translate_cache(pool, cache, &request10,
                    &my_translate_handler, nullptr, &async_ref);

    static const TranslateRequest request6 = {
        .uri = "/cgi1/foo",
    };
    static const struct cgi_address cgi6 = {
        .path = "/usr/lib/cgi-bin/cgi.pl",
        .uri = "/cgi1/foo",
        .path_info = "x/foo",
    };
    static const TranslateResponse response6 = {
        .address = {
            .type = RESOURCE_ADDRESS_CGI,
            .u = {
                .cgi = &cgi6,
            },
        },
        .base = "/cgi1/",
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
    };

    next_response = expected_response = &response6;
    translate_cache(pool, cache, &request6,
                    &my_translate_handler, nullptr, &async_ref);

    static const TranslateRequest request7 = {
        .uri = "/cgi1/a/b/c",
    };
    static const struct cgi_address cgi7 = {
        .path = "/usr/lib/cgi-bin/cgi.pl",
        .uri = "/cgi1/a/b/c",
        .path_info = "x/a/b/c",
    };
    static const TranslateResponse response7 = {
        .address = {
            .type = RESOURCE_ADDRESS_CGI,
            .u = {
                .cgi = &cgi7,
            },
        },
        .base = "/cgi1/",
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
    };

    next_response = nullptr;
    expected_response = &response7;
    translate_cache(pool, cache, &request7,
                    &my_translate_handler, nullptr, &async_ref);

    static const TranslateRequest request8 = {
        .uri = "/cgi2/foo",
    };
    static const struct cgi_address cgi8 = {
        .path = "/usr/lib/cgi-bin/cgi.pl",
        .uri = "/cgi2/foo",
        .path_info = "foo",
    };
    static const TranslateResponse response8 = {
        .address = {
            .type = RESOURCE_ADDRESS_CGI,
            .u = {
                .cgi = &cgi8,
            },
        },
        .base = "/cgi2/",
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
    };

    next_response = expected_response = &response8;
    translate_cache(pool, cache, &request8,
                    &my_translate_handler, nullptr, &async_ref);

    static const TranslateRequest request9 = {
        .uri = "/cgi2/a/b/c",
    };
    static const struct cgi_address cgi9 = {
        .path = "/usr/lib/cgi-bin/cgi.pl",
        .uri = "/cgi2/a/b/c",
        .path_info = "a/b/c",
    };
    static const TranslateResponse response9 = {
        .address = {
            .type = RESOURCE_ADDRESS_CGI,
            .u = {
                .cgi = &cgi9,
            },
        },
        .base = "/cgi2/",
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
    };

    next_response = nullptr;
    expected_response = &response9;
    translate_cache(pool, cache, &request9,
                    &my_translate_handler, nullptr, &async_ref);
}

/**
 * Feed the cache with a request to the BASE.  This was buggy until
 * 4.0.30.
 */
static void
test_base_root(struct pool *pool, struct tcache *cache)
{
    struct async_operation_ref async_ref;

    static constexpr TranslateRequest request1 = {
        .uri = "/base_root/",
    };
    static const struct file_address file1("/var/www/");
    static const TranslateResponse response1 = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .file = &file1,
            },
        },
        .base = "/base_root/",
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
    };

    next_response = expected_response = &response1;
    translate_cache(pool, cache, &request1,
                    &my_translate_handler, nullptr, &async_ref);

    static constexpr TranslateRequest request2 = {
        .uri = "/base_root/hansi",
    };
    static const struct file_address file2("/var/www/hansi");
    static const TranslateResponse response2 = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .file = &file2,
            },
        },
        .base = "/base_root/",
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
    };

    next_response = nullptr;
    expected_response = &response2;
    translate_cache(pool, cache, &request2,
                    &my_translate_handler, nullptr, &async_ref);
}

static void
test_base_mismatch(struct pool *pool, struct tcache *cache)
{
    struct async_operation_ref async_ref;

    static constexpr TranslateRequest request1 = {
        .uri = "/base_mismatch/hansi",
    };
    static const struct file_address file1("/var/www/");
    static const TranslateResponse response1 = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .file = &file1,
            },
        },
        .base = "/different_base/",
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
    };

    next_response = &response1;
    expected_response = nullptr;
    translate_cache(pool, cache, &request1,
                    &my_translate_handler, nullptr, &async_ref);
}

/**
 * Test BASE+URI.
 */
static void
test_base_uri(struct pool *pool, struct tcache *cache)
{
    struct async_operation_ref async_ref;

    static constexpr TranslateRequest request1 = {
        .uri = "/base_uri/foo",
    };
    static const struct file_address file1("/var/www/foo");
    static const TranslateResponse response1 = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .file = &file1,
            },
        },
        .base = "/base_uri/",
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
        .uri = "/modified/foo",
    };

    next_response = expected_response = &response1;
    translate_cache(pool, cache, &request1,
                    &my_translate_handler, nullptr, &async_ref);

    static constexpr TranslateRequest request2 = {
        .uri = "/base_uri/hansi",
    };
    static const struct file_address file2("/var/www/hansi");
    static const TranslateResponse response2 = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .file = &file2,
            },
        },
        .base = "/base_uri/",
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
        .uri = "/modified/hansi",
    };

    next_response = nullptr;
    expected_response = &response2;
    translate_cache(pool, cache, &request2,
                    &my_translate_handler, nullptr, &async_ref);
}

/**
 * Test BASE+TEST_PATH.
 */
static void
test_base_test_path(struct pool *pool, struct tcache *cache)
{
    struct async_operation_ref async_ref;

    static constexpr TranslateRequest request1 = {
        .uri = "/base_test_path/foo",
    };
    static const struct file_address file1("/var/www/foo");
    static const TranslateResponse response1 = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .file = &file1,
            },
        },
        .base = "/base_test_path/",
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
        .test_path = "/modified/foo",
    };

    next_response = expected_response = &response1;
    translate_cache(pool, cache, &request1,
                    &my_translate_handler, nullptr, &async_ref);

    static constexpr TranslateRequest request2 = {
        .uri = "/base_test_path/hansi",
    };
    static const struct file_address file2("/var/www/hansi");
    static const TranslateResponse response2 = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .file = &file2,
            },
        },
        .base = "/base_test_path/",
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
        .test_path = "/modified/hansi",
    };

    next_response = nullptr;
    expected_response = &response2;
    translate_cache(pool, cache, &request2,
                    &my_translate_handler, nullptr, &async_ref);
}

static void
test_easy_base(struct pool *pool, struct tcache *cache)
{
    static const TranslateRequest request1 = {
        .uri = "/easy/bar.html",
    };
    static const TranslateRequest request2 = {
        .uri = "/easy/index.html",
    };

    static const struct file_address file1("/var/www/");
    static const TranslateResponse response1 = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .file = &file1,
            },
        },
        .base = "/easy/",
        .easy_base = true,
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
    };

    static const struct file_address file1b("/var/www/bar.html");
    static const TranslateResponse response1b = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .file = &file1b,
            },
        },
        .base = "/easy/",
        .easy_base = true,
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
    };

    static const struct file_address file2("/var/www/index.html");
    static const TranslateResponse response2 = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .file = &file2,
            },
        },
        .base = "/easy/",
        .easy_base = true,
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
    };

    struct async_operation_ref async_ref;

    next_response = &response1;
    expected_response = &response1b;
    translate_cache(pool, cache, &request1,
                    &my_translate_handler, nullptr, &async_ref);

    next_response = nullptr;
    translate_cache(pool, cache, &request1,
                    &my_translate_handler, nullptr, &async_ref);

    expected_response = &response2;
    translate_cache(pool, cache, &request2,
                    &my_translate_handler, nullptr, &async_ref);
}

/**
 * Test EASY_BASE+URI.
 */
static void
test_easy_base_uri(struct pool *pool, struct tcache *cache)
{
    struct async_operation_ref async_ref;

    static constexpr TranslateRequest request1 = {
        .uri = "/easy_base_uri/foo",
    };

    static const struct file_address file1("/var/www/");
    static const TranslateResponse response1 = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .file = &file1,
            },
        },
        .base = "/easy_base_uri/",
        .easy_base = true,
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
        .uri = "/modified/",
    };

    static const struct file_address file1b("/var/www/foo");
    static const TranslateResponse response1b = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .file = &file1b,
            },
        },
        .base = "/easy_base_uri/",
        .easy_base = true,
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
        .uri = "/modified/foo",
    };

    next_response = &response1;
    expected_response = &response1b;
    translate_cache(pool, cache, &request1,
                    &my_translate_handler, nullptr, &async_ref);

    static constexpr TranslateRequest request2 = {
        .uri = "/easy_base_uri/hansi",
    };
    static const struct file_address file2("/var/www/hansi");
    static const TranslateResponse response2 = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .file = &file2,
            },
        },
        .base = "/easy_base_uri/",
        .easy_base = true,
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
        .uri = "/modified/hansi",
    };

    next_response = nullptr;
    expected_response = &response2;
    translate_cache(pool, cache, &request2,
                    &my_translate_handler, nullptr, &async_ref);
}

/**
 * Test EASY_BASE + TEST_PATH.
 */
static void
test_easy_base_test_path(struct pool *pool, struct tcache *cache)
{
    struct async_operation_ref async_ref;

    static constexpr TranslateRequest request1 = {
        .uri = "/easy_base_test_path/foo",
    };

    static const struct file_address file1("/var/www/");
    static const TranslateResponse response1 = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .file = &file1,
            },
        },
        .base = "/easy_base_test_path/",
        .easy_base = true,
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
        .test_path = "/modified/",
    };

    static const struct file_address file1b("/var/www/foo");
    static const TranslateResponse response1b = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .file = &file1b,
            },
        },
        .base = "/easy_base_test_path/",
        .easy_base = true,
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
        .test_path = "/modified/foo",
    };

    next_response = &response1;
    expected_response = &response1b;
    translate_cache(pool, cache, &request1,
                    &my_translate_handler, nullptr, &async_ref);

    static constexpr TranslateRequest request2 = {
        .uri = "/easy_base_test_path/hansi",
    };
    static const struct file_address file2("/var/www/hansi");
    static const TranslateResponse response2 = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .file = &file2,
            },
        },
        .base = "/easy_base_test_path/",
        .easy_base = true,
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
        .test_path = "/modified/hansi",
    };

    next_response = nullptr;
    expected_response = &response2;
    translate_cache(pool, cache, &request2,
                    &my_translate_handler, nullptr, &async_ref);
}

static void
test_vary_invalidate(struct pool *pool, struct tcache *cache)
{
    static const TranslateRequest request6 = {
        .uri = "/qs",
        .query_string = "abc",
    };
    static const TranslateRequest request7 = {
        .uri = "/qs",
        .query_string = "xyz",
    };
    static const TranslateRequest request8 = {
        .uri = "/qs/",
        .query_string = "xyz",
    };
    static const uint16_t response5_vary[] = {
        TRANSLATE_QUERY_STRING,
    };

    static const struct file_address file5a("/srv/qs1");
    static const TranslateResponse response5a = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .file = &file5a,
            },
        },
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
        .vary = { response5_vary, sizeof(response5_vary) / sizeof(response5_vary[0]), },
    };

    static const struct file_address file5b("/srv/qs2");
    static const TranslateResponse response5b = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .file = &file5b,
            },
        },
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
        .vary = { response5_vary, sizeof(response5_vary) / sizeof(response5_vary[0]), },
    };

    static const uint16_t response5_invalidate[] = {
        TRANSLATE_QUERY_STRING,
    };

    static const struct file_address file5c("/srv/qs3");
    static const TranslateResponse response5c = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .file = &file5c,
            },
        },
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
        .vary = { response5_vary, sizeof(response5_vary) / sizeof(response5_vary[0]), },
        .invalidate = { response5_invalidate, sizeof(response5_invalidate) / sizeof(response5_invalidate[0]) },
    };

    struct async_operation_ref async_ref;

    next_response = expected_response = &response5a;
    translate_cache(pool, cache, &request6,
                    &my_translate_handler, nullptr, &async_ref);

    next_response = expected_response = &response5b;
    translate_cache(pool, cache, &request7,
                    &my_translate_handler, nullptr, &async_ref);

    next_response = nullptr;
    expected_response = &response5a;
    translate_cache(pool, cache, &request6,
                    &my_translate_handler, nullptr, &async_ref);

    next_response = nullptr;
    expected_response = &response5b;
    translate_cache(pool, cache, &request7,
                    &my_translate_handler, nullptr, &async_ref);

    next_response = expected_response = &response5c;
    translate_cache(pool, cache, &request8,
                    &my_translate_handler, nullptr, &async_ref);

    next_response = nullptr;
    expected_response = &response5a;
    translate_cache(pool, cache, &request6,
                    &my_translate_handler, nullptr, &async_ref);

    next_response = expected_response = &response5c;
    translate_cache(pool, cache, &request7,
                    &my_translate_handler, nullptr, &async_ref);

    next_response = expected_response = &response5c;
    translate_cache(pool, cache, &request8,
                    &my_translate_handler, nullptr, &async_ref);

    expected_response = &response5c;
    translate_cache(pool, cache, &request7,
                    &my_translate_handler, nullptr, &async_ref);
}

static void
test_invalidate_uri(struct pool *pool, struct tcache *cache)
{
    struct async_operation_ref async_ref;

    /* feed the cache */

    static const TranslateRequest request1 = {
        .uri = "/invalidate/uri",
    };
    static const struct file_address file1("/var/www/invalidate/uri");
    static const TranslateResponse response1 = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .file = &file1,
            },
        },
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
    };

    next_response = expected_response = &response1;
    translate_cache(pool, cache, &request1,
                    &my_translate_handler, nullptr, &async_ref);

    static const TranslateRequest request2 = {
        .uri = "/invalidate/uri",
        .check = { "x", 1 },
    };
    static const TranslateResponse response2 = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .file = &file1,
            },
        },
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
    };

    next_response = expected_response = &response2;
    translate_cache(pool, cache, &request2,
                    &my_translate_handler, nullptr, &async_ref);

    static const TranslateRequest request3 = {
        .uri = "/invalidate/uri",
        .error_document_status = HTTP_STATUS_INTERNAL_SERVER_ERROR,
    };
    static const struct file_address file3("/var/www/500/invalidate/uri");
    static const TranslateResponse response3 = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .file = &file3,
            },
        },
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
    };

    next_response = expected_response = &response3;
    translate_cache(pool, cache, &request3,
                    &my_translate_handler, nullptr, &async_ref);

    static const TranslateRequest request4 = {
        .uri = "/invalidate/uri",
        .error_document_status = HTTP_STATUS_INTERNAL_SERVER_ERROR,
        .check = { "x", 1 },
    };
    static const struct file_address file4("/var/www/500/check/invalidate/uri");
    static const TranslateResponse response4 = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .file = &file4,
            },
        },
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
    };

    next_response = expected_response = &response4;
    translate_cache(pool, cache, &request4,
                    &my_translate_handler, nullptr, &async_ref);

    static const TranslateRequest request4b = {
        .uri = "/invalidate/uri",
        .error_document_status = HTTP_STATUS_INTERNAL_SERVER_ERROR,
        .check = { "x", 1 },
        .want_full_uri = { "a\0/b", 4 },
    };
    static const struct file_address file4b("/var/www/500/check/wfu/invalidate/uri");
    static const TranslateResponse response4b = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .file = &file4b,
            },
        },
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
    };

    next_response = expected_response = &response4b;
    translate_cache(pool, cache, &request4b,
                    &my_translate_handler, nullptr, &async_ref);

    /* verify the cache items */

    next_response = nullptr;

    expected_response = &response1;
    translate_cache(pool, cache, &request1,
                    &my_translate_handler, nullptr, &async_ref);

    expected_response = &response2;
    translate_cache(pool, cache, &request2,
                    &my_translate_handler, nullptr, &async_ref);

    expected_response = &response3;
    translate_cache(pool, cache, &request3,
                    &my_translate_handler, nullptr, &async_ref);

    expected_response = &response4;
    translate_cache(pool, cache, &request4,
                    &my_translate_handler, nullptr, &async_ref);

    expected_response = &response4b;
    translate_cache(pool, cache, &request4b,
                    &my_translate_handler, nullptr, &async_ref);

    /* invalidate all cache items */

    static const TranslateRequest request5 = {
        .uri = "/invalidate/uri",
        .error_document_status = HTTP_STATUS_NOT_FOUND,
    };
    static const uint16_t response5_invalidate[] = {
        TRANSLATE_URI,
    };
    static const struct file_address file5("/var/www/404/invalidate/uri");
    static const TranslateResponse response5 = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .file = &file5,
            },
        },
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
        .invalidate = { response5_invalidate, sizeof(response5_invalidate) / sizeof(response5_invalidate[0]) },
    };

    next_response = expected_response = &response5;
    translate_cache(pool, cache, &request5,
                    &my_translate_handler, nullptr, &async_ref);

    /* check if all cache items have really been deleted */

    next_response = expected_response = nullptr;

    translate_cache(pool, cache, &request1,
                    &my_translate_handler, nullptr, &async_ref);
    translate_cache(pool, cache, &request2,
                    &my_translate_handler, nullptr, &async_ref);
    translate_cache(pool, cache, &request3,
                    &my_translate_handler, nullptr, &async_ref);
    translate_cache(pool, cache, &request4,
                    &my_translate_handler, nullptr, &async_ref);
    translate_cache(pool, cache, &request4b,
                    &my_translate_handler, nullptr, &async_ref);
}

static void
test_regex(struct pool *pool, struct tcache *cache)
{
    static const TranslateRequest request_i1 = {
        .uri = "/regex/foo",
    };
    static const struct file_address file_i1("/var/www/regex/other/foo");
    static const TranslateResponse response_i1 = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .file = &file_i1,
            },
        },
        .base = "/regex/",
        .inverse_regex = "\\.(jpg|html)$",
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
    };

    static const TranslateRequest request_i2 = {
        .uri = "/regex/bar",
    };
    static const struct file_address file_i2("/var/www/regex/other/bar");
    static const TranslateResponse response_i2 = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .file = &file_i2,
            },
        },
        .base = "/regex/",
        .inverse_regex = "\\.(jpg|html)$",
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
    };

    static const TranslateRequest request1 = {
        .uri = "/regex/a/foo.jpg",
    };
    static const struct file_address file1("/var/www/regex/images/a/foo.jpg");
    static const TranslateResponse response1 = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .file = &file1,
            },
        },
        .base = "/regex/",
        .regex = "\\.jpg$",
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
    };

    static const TranslateRequest request2 = {
        .uri = "/regex/b/foo.html",
    };
    static const struct file_address file2("/var/www/regex/html/b/foo.html");
    static const TranslateResponse response2 = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .file = &file2,
            },
        },
        .base = "/regex/",
        .regex = "\\.html$",
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
    };

    static const TranslateRequest request3 = {
        .uri = "/regex/c/bar.jpg",
    };
    static const struct file_address file3("/var/www/regex/images/c/bar.jpg");
    static const TranslateResponse response3 = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .file = &file3,
            },
        },
        .base = "/regex/",
        .regex = "\\.jpg$",
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
    };

    static const TranslateRequest request4 = {
        .uri = "/regex/d/bar.html",
    };
    static const struct file_address file4("/var/www/regex/html/d/bar.html");
    static const TranslateResponse response4 = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .file = &file4,
            },
        },
        .base = "/regex/",
        .regex = "\\.html$",
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
    };

    struct async_operation_ref async_ref;

    /* add the "inverse_regex" test to the cache first */
    next_response = expected_response = &response_i1;
    translate_cache(pool, cache, &request_i1,
                    &my_translate_handler, nullptr, &async_ref);

    /* fill the cache */
    next_response = expected_response = &response1;
    translate_cache(pool, cache, &request1,
                    &my_translate_handler, nullptr, &async_ref);

    /* regex mismatch */
    next_response = expected_response = &response2;
    translate_cache(pool, cache, &request2,
                    &my_translate_handler, nullptr, &async_ref);

    /* regex match */
    next_response = nullptr;
    expected_response = &response3;
    translate_cache(pool, cache, &request3,
                    &my_translate_handler, nullptr, &async_ref);

    /* second regex match */
    next_response = nullptr;
    expected_response = &response4;
    translate_cache(pool, cache, &request4,
                    &my_translate_handler, nullptr, &async_ref);

    /* see if the "inverse_regex" cache item is still there */
    next_response = nullptr;
    expected_response = &response_i2;
    translate_cache(pool, cache, &request_i2,
                    &my_translate_handler, nullptr, &async_ref);
}

static void
test_regex_error(struct pool *pool, struct tcache *cache)
{
    static const TranslateRequest request = {
        .uri = "/regex-error",
    };
    static const struct file_address file("/error");
    static const TranslateResponse response = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .file = &file,
            },
        },
        .base = "/regex/",
        .regex = "(",
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
    };

    struct async_operation_ref async_ref;

    /* this must fail */
    next_response = &response;
    expected_response = nullptr;
    translate_cache(pool, cache, &request,
                    &my_translate_handler, nullptr, &async_ref);
}

static void
test_regex_tail(struct pool *pool, struct tcache *cache)
{
    struct async_operation_ref async_ref;

    static const TranslateRequest request1 = {
        .uri = "/regex_tail/a/foo.jpg",
    };
    static const struct file_address file1("/var/www/regex/images/a/foo.jpg");
    static const TranslateResponse response1 = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .file = &file1,
            },
        },
        .base = "/regex_tail/",
        .regex = "^a/",
        .regex_tail = true,
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
    };

    next_response = expected_response = &response1;
    translate_cache(pool, cache, &request1,
                    &my_translate_handler, nullptr, &async_ref);

    static const TranslateRequest request2 = {
        .uri = "/regex_tail/b/foo.html",
    };

    next_response = expected_response = nullptr;
    translate_cache(pool, cache, &request2,
                    &my_translate_handler, nullptr, &async_ref);

    static const TranslateRequest request3 = {
        .uri = "/regex_tail/a/bar.jpg",
    };
    static const struct file_address file3("/var/www/regex/images/a/bar.jpg");
    static const TranslateResponse response3 = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .file = &file3,
            },
        },
        .base = "/regex_tail/",
        .regex = "^a/",
        .regex_tail = true,
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
    };

    next_response = nullptr;
    expected_response = &response3;
    translate_cache(pool, cache, &request3,
                    &my_translate_handler, nullptr, &async_ref);

    static const TranslateRequest request4 = {
        .uri = "/regex_tail/%61/escaped.html",
    };

    next_response = expected_response = nullptr;
    translate_cache(pool, cache, &request4,
                    &my_translate_handler, nullptr, &async_ref);
}

static void
test_regex_tail_unescape(struct pool *pool, struct tcache *cache)
{
    struct async_operation_ref async_ref;

    static const TranslateRequest request1 = {
        .uri = "/regex_unescape/a/foo.jpg",
    };
    static const struct file_address file1("/var/www/regex/images/a/foo.jpg");
    static const TranslateResponse response1 = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .file = &file1,
            },
        },
        .base = "/regex_unescape/",
        .regex = "^a/",
        .regex_tail = true,
        .regex_unescape = true,
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
    };

    next_response = expected_response = &response1;
    translate_cache(pool, cache, &request1,
                    &my_translate_handler, nullptr, &async_ref);

    static const TranslateRequest request2 = {
        .uri = "/regex_unescape/b/foo.html",
    };

    next_response = expected_response = nullptr;
    translate_cache(pool, cache, &request2,
                    &my_translate_handler, nullptr, &async_ref);

    static const TranslateRequest request3 = {
        .uri = "/regex_unescape/a/bar.jpg",
    };
    static const struct file_address file3("/var/www/regex/images/a/bar.jpg");
    static const TranslateResponse response3 = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .file = &file3,
            },
        },
        .base = "/regex_unescape/",
        .regex = "^a/",
        .regex_tail = true,
        .regex_unescape = true,
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
    };

    next_response = nullptr;
    expected_response = &response3;
    translate_cache(pool, cache, &request3,
                    &my_translate_handler, nullptr, &async_ref);

    static const TranslateRequest request4 = {
        .uri = "/regex_unescape/%61/escaped.html",
    };
    static const struct file_address file4("/var/www/regex/images/a/escaped.html");
    static const TranslateResponse response4 = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .file = &file4,
            },
        },
        .base = "/regex_unescape/",
        .regex = "^a/",
        .regex_tail = true,
        .regex_unescape = true,
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
    };

    next_response = nullptr;
    expected_response = &response4;
    translate_cache(pool, cache, &request4,
                    &my_translate_handler, nullptr, &async_ref);
}

static void
test_expand(struct pool *pool, struct tcache *cache)
{
    struct async_operation_ref async_ref;

    /* add to cache */

    static const TranslateRequest request1 = {
        .uri = "/regex-expand/b=c",
    };
    static const struct cgi_address cgi1 = {
        .path = "/usr/lib/cgi-bin/foo.cgi",
        .expand_path_info = "/a/\\1",
    };
    static const TranslateResponse response1n = {
        .address = {
            .type = RESOURCE_ADDRESS_CGI,
            .u = {
                .cgi = &cgi1,
            },
        },
        .base = "/regex-expand/",
        .regex = "^/regex-expand/(.+=.+)$",
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
    };
    static const struct cgi_address cgi1e = {
        .path = "/usr/lib/cgi-bin/foo.cgi",
        .path_info = "/a/b=c",
        .expand_path_info = "/a/\\1",
    };
    static const TranslateResponse response1e = {
        .address = {
            .type = RESOURCE_ADDRESS_CGI,
            .u = {
                .cgi = &cgi1e,
            },
        },
        .base = "/regex-expand/",
        .regex = "^/regex-expand/(.+=.+)$",
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
    };

    next_response = &response1n;
    expected_response = &response1e;
    translate_cache(pool, cache, &request1,
                    &my_translate_handler, nullptr, &async_ref);

    /* check match */

    static const TranslateRequest request2 = {
        .uri = "/regex-expand/d=e",
    };
    static const struct cgi_address cgi2 = {
        .path = "/usr/lib/cgi-bin/foo.cgi",
        .path_info = "/a/d=e",
    };
    static const TranslateResponse response2 = {
        .address = {
            .type = RESOURCE_ADDRESS_CGI,
            .u = {
                .cgi = &cgi2,
            },
        },
        .base = "/regex-expand/",
        .regex = "^/regex-expand/(.+=.+)$",
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
    };

    next_response = nullptr;
    expected_response = &response2;
    translate_cache(pool, cache, &request2,
                    &my_translate_handler, nullptr, &async_ref);
}

static void
test_expand_local(struct pool *pool, struct tcache *cache)
{
    struct async_operation_ref async_ref;

    /* add to cache */

    static const TranslateRequest request1 = {
        .uri = "/regex-expand2/foo/bar.jpg/b=c",
    };
    static struct file_address file1n("/dummy");
    file1n.expand_path = "/var/www/\\1";
    static const TranslateResponse response1n = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .file = &file1n,
            },
        },
        .base = "/regex-expand2/",
        .regex = "^/regex-expand2/(.+\\.jpg)/([^/]+=[^/]+)$",
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
    };

    static struct file_address file1e("/var/www/foo/bar.jpg");
    file1e.expand_path = "/var/www/\\1";
    static const TranslateResponse response1e = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .file = &file1e,
            },
        },
        .base = "/regex-expand2/",
        .regex = "^/regex-expand2/(.+\\.jpg)/([^/]+=[^/]+)$",
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
    };

    next_response = &response1n;
    expected_response = &response1e;
    translate_cache(pool, cache, &request1,
                    &my_translate_handler, nullptr, &async_ref);

    /* check match */

    static const TranslateRequest request2 = {
        .uri = "/regex-expand2/x/y/z.jpg/d=e",
    };
    static const struct file_address file2("/var/www/x/y/z.jpg");
    static const TranslateResponse response2 = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .file = &file2,
            },
        },
        .base = "/regex-expand2/",
        .regex = "^/regex-expand2/(.+\\.jpg)/([^/]+=[^/]+)$",
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
    };

    next_response = nullptr;
    expected_response = &response2;
    translate_cache(pool, cache, &request2,
                    &my_translate_handler, nullptr, &async_ref);
}

static void
test_expand_local_filter(struct pool *pool, struct tcache *cache)
{
    struct async_operation_ref async_ref;

    /* add to cache */

    static const TranslateRequest request1 = {
        .uri = "/regex-expand3/foo/bar.jpg/b=c",
    };
    static const struct cgi_address cgi1n = {
        .path = "/usr/lib/cgi-bin/image-processor.cgi",
        .expand_path_info = "/\\2",
    };
    static Transformation transformation1n = {
        .type = Transformation::Type::FILTER,
        .u.filter = {
            .type = RESOURCE_ADDRESS_CGI,
            .u = {
                .cgi = &cgi1n,
            },
        },
    };
    static WidgetView view1n = {
        .transformation = &transformation1n,
    };
    static struct file_address file1n("/dummy");
    file1n.expand_path = "/var/www/\\1";
    static const TranslateResponse response1n = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .file = &file1n,
            },
        },
        .base = "/regex-expand3/",
        .regex = "^/regex-expand3/(.+\\.jpg)/([^/]+=[^/]+)$",
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
        .views = &view1n,
    };

    static const struct cgi_address cgi1e = {
        .path = "/usr/lib/cgi-bin/image-processor.cgi",
        .path_info = "/b=c",
        .expand_path_info = "/\\2",
    };
    static Transformation transformation1e = {
        .type = Transformation::Type::FILTER,
        .u.filter = {
            .type = RESOURCE_ADDRESS_CGI,
            .u = {
                .cgi = &cgi1e,
            },
        },
    };
    static WidgetView view1e = {
        .transformation = &transformation1e,
    };
    static struct file_address file1e("/var/www/foo/bar.jpg");
    file1e.expand_path = "/var/www/\\1";
    static const TranslateResponse response1e = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .file = &file1e,
            },
        },
        .base = "/regex-expand3/",
        .regex = "^/regex-expand3/(.+\\.jpg)/([^/]+=[^/]+)$",
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
        .views = &view1e,
    };

    next_response = &response1n;
    expected_response = &response1e;
    translate_cache(pool, cache, &request1,
                    &my_translate_handler, nullptr, &async_ref);

    /* check match */

    static const TranslateRequest request2 = {
        .uri = "/regex-expand3/x/y/z.jpg/d=e",
    };
    static const struct cgi_address cgi2 = {
        .path = "/usr/lib/cgi-bin/image-processor.cgi",
        .path_info = "/d=e",
        .expand_path_info = "/\\2",
    };
    static Transformation transformation2 = {
        .type = Transformation::Type::FILTER,
        .u.filter = {
            .type = RESOURCE_ADDRESS_CGI,
            .u = {
                .cgi = &cgi2,
            },
        },
    };
    static WidgetView view2 = {
        .transformation = &transformation2,
    };
    static const struct file_address file2("/var/www/x/y/z.jpg");
    static const TranslateResponse response2 = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .file = &file2,
            },
        },
        .base = "/regex-expand3/",
        .regex = "^/regex-expand3/(.+\\.jpg)/([^/]+=[^/]+)$",
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
        .views = &view2,
    };

    next_response = nullptr;
    expected_response = &response2;
    translate_cache(pool, cache, &request2,
                    &my_translate_handler, nullptr, &async_ref);
}

static void
test_expand_uri(struct pool *pool, struct tcache *cache)
{
    struct async_operation_ref async_ref;

    /* add to cache */

    static const TranslateRequest request1 = {
        .uri = "/regex-expand4/foo/bar.jpg/b=c",
    };
    static const struct http_address uwa1n = {
        .scheme = URI_SCHEME_HTTP,
        .host_and_port = "localhost:8080",
        .path = "/foo/bar.jpg",
        .expand_path = "/\\1",
    };
    static const TranslateResponse response1n = {
        .address = {
            .type = RESOURCE_ADDRESS_HTTP,
            .u = {
                .http = &uwa1n,
            },
        },
        .base = "/regex-expand4/",
        .regex = "^/regex-expand4/(.+\\.jpg)/([^/]+=[^/]+)$",
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
    };
    static const struct http_address uwa1e = {
        .scheme = URI_SCHEME_HTTP,
        .host_and_port = "localhost:8080",
        .path = "/foo/bar.jpg",
    };
    static const TranslateResponse response1e = {
        .address = {
            .type = RESOURCE_ADDRESS_HTTP,
            .u = {
                .http = &uwa1e,
            },
        },
        .base = "/regex-expand4/",
        .regex = "^/regex-expand4/(.+\\.jpg)/([^/]+=[^/]+)$",
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
    };

    next_response = &response1n;
    expected_response = &response1e;
    translate_cache(pool, cache, &request1,
                    &my_translate_handler, nullptr, &async_ref);

    /* check match */

    static const TranslateRequest request2 = {
        .uri = "/regex-expand4/x/y/z.jpg/d=e",
    };
    static const struct http_address uwa2 = {
        .scheme = URI_SCHEME_HTTP,
        .host_and_port = "localhost:8080",
        .path = "/x/y/z.jpg",
    };
    static const TranslateResponse response2 = {
        .address = {
            .type = RESOURCE_ADDRESS_HTTP,
            .u = {
                .http = &uwa2,
            },
        },
        .base = "/regex-expand4/",
        .regex = "^/regex-expand4/(.+\\.jpg)/([^/]+=[^/]+)$",
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
    };

    next_response = nullptr;
    expected_response = &response2;
    translate_cache(pool, cache, &request2,
                    &my_translate_handler, nullptr, &async_ref);
}

static void
test_auto_base(struct pool *pool, struct tcache *cache)
{
    struct async_operation_ref async_ref;

    /* store response */

    static const TranslateRequest request1 = {
        .uri = "/auto-base/foo.cgi/bar",
    };
    static const struct cgi_address cgi1n = {
        .path = "/usr/lib/cgi-bin/foo.cgi",
        .uri = "/auto-base/foo.cgi/bar",
        .path_info = "/bar",
    };
    static const TranslateResponse response1n = {
        .address = {
            .type = RESOURCE_ADDRESS_CGI,
            .u = {
                .cgi = &cgi1n,
            },
        },
        .auto_base = true,
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
    };
    static const TranslateResponse response1e = {
        .address = {
            .type = RESOURCE_ADDRESS_CGI,
            .u = {
                .cgi = &cgi1n,
            },
        },
        .auto_base = true,
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
    };

    next_response = &response1n;
    expected_response = &response1e;
    translate_cache(pool, cache, &request1,
                    &my_translate_handler, nullptr, &async_ref);

    /* check if BASE was auto-detected */

    static const TranslateRequest request2 = {
        .uri = "/auto-base/foo.cgi/check",
    };
    static const struct cgi_address cgi2 = {
        .path = "/usr/lib/cgi-bin/foo.cgi",
        .uri = "/auto-base/foo.cgi/check",
        .path_info = "/check",
    };
    static const TranslateResponse response2 = {
        .address = {
            .type = RESOURCE_ADDRESS_CGI,
            .u = {
                .cgi = &cgi2,
            },
        },
        .base = "/auto-base/foo.cgi/",
        .auto_base = true,
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
    };

    next_response = nullptr;
    expected_response = &response2;
    translate_cache(pool, cache, &request2,
                    &my_translate_handler, nullptr, &async_ref);
}

/**
 * Test CHECK + BASE.
 */
static void
test_base_check(struct pool *pool, struct tcache *cache)
{
    struct async_operation_ref async_ref;

    /* feed the cache */

    static const TranslateRequest request1 = {
        .uri = "/a/b/c.html",
    };
    static const TranslateResponse response1 = {
        .address = {
            .type = RESOURCE_ADDRESS_NONE,
        },
        .base = "/a/",
        .check = { "x", 1 },
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
    };

    next_response = expected_response = &response1;
    translate_cache(pool, cache, &request1,
                    &my_translate_handler, nullptr, &async_ref);

    static const TranslateRequest request2 = {
        .uri = "/a/b/c.html",
        .check = { "x", 1 },
    };
    static const struct file_address file2("/var/www/vol0/a/b/c.html");
    static const TranslateResponse response2 = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .file = &file2,
            },
        },
        .base = "/a/b/",
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
    };

    next_response = expected_response = &response2;
    translate_cache(pool, cache, &request2,
                    &my_translate_handler, nullptr, &async_ref);

    static const TranslateRequest request3 = {
        .uri = "/a/d/e.html",
        .check = { "x", 1 },
    };
    static const struct file_address file3("/var/www/vol1/a/d/e.html");
    static const TranslateResponse response3 = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .file = &file3,
            },
        },
        .base = "/a/d/",
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
    };

    next_response = expected_response = &response3;
    translate_cache(pool, cache, &request3,
                    &my_translate_handler, nullptr, &async_ref);

    /* now check whether the translate cache matches the BASE
       correctly */

    next_response = nullptr;

    static const TranslateRequest request4 = {
        .uri = "/a/f/g.html",
    };
    static const TranslateResponse response4 = {
        .address = {
            .type = RESOURCE_ADDRESS_NONE,
        },
        .base = "/a/",
        .check = { "x", 1 },
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
    };

    expected_response = &response4;
    translate_cache(pool, cache, &request4,
                    &my_translate_handler, nullptr, &async_ref);

    static const TranslateRequest request5 = {
        .uri = "/a/b/0/1.html",
    };

    translate_cache(pool, cache, &request5,
                    &my_translate_handler, nullptr, &async_ref);

    static const TranslateRequest request6 = {
        .uri = "/a/b/0/1.html",
        .check = { "x", 1 },
    };
    static const struct file_address file6("/var/www/vol0/a/b/0/1.html");
    static const TranslateResponse response6 = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .file = &file6,
            },
        },
        .base = "/a/b/",
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
    };

    expected_response = &response6;
    translate_cache(pool, cache, &request6,
                    &my_translate_handler, nullptr, &async_ref);

    static const TranslateRequest request7 = {
        .uri = "/a/d/2/3.html",
        .check = { "x", 1 },
    };
    static const struct file_address file7("/var/www/vol1/a/d/2/3.html");
    static const TranslateResponse response7 = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .file = &file7,
            },
        },
        .base = "/a/d/",
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
    };

    expected_response = &response7;
    translate_cache(pool, cache, &request7,
                    &my_translate_handler, nullptr, &async_ref);

    /* expect cache misses */

    expected_response = nullptr;

    static const TranslateRequest miss1 = {
        .uri = "/a/f/g.html",
        .check = { "y", 1 },
    };

    translate_cache(pool, cache, &miss1,
                    &my_translate_handler, nullptr, &async_ref);
}

/**
 * Test WANT_FULL_URI + BASE.
 */
static void
test_base_wfu(struct pool *pool, struct tcache *cache)
{
    struct async_operation_ref async_ref;

    /* feed the cache */

    static const TranslateRequest request1 = {
        .uri = "/wfu/a/b/c.html",
    };
    static const TranslateResponse response1 = {
        .address = {
            .type = RESOURCE_ADDRESS_NONE,
        },
        .base = "/wfu/a/",
        .want_full_uri = { "x", 1 },
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
    };

    next_response = expected_response = &response1;
    translate_cache(pool, cache, &request1,
                    &my_translate_handler, nullptr, &async_ref);

    static const TranslateRequest request2 = {
        .uri = "/wfu/a/b/c.html",
        .want_full_uri = { "x", 1 },
    };
    static const struct file_address file2("/var/www/vol0/a/b/c.html");
    static const TranslateResponse response2 = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,

            .u = {
                .file = &file2,
            },
        },
        .base = "/wfu/a/b/",
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
    };

    next_response = expected_response = &response2;
    translate_cache(pool, cache, &request2,
                    &my_translate_handler, nullptr, &async_ref);

    static const TranslateRequest request3 = {
        .uri = "/wfu/a/d/e.html",
        .want_full_uri = { "x", 1 },
    };
    static const struct file_address file3("/var/www/vol1/a/d/e.html");
    static const TranslateResponse response3 = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .file = &file3,
            },
        },
        .base = "/wfu/a/d/",
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
    };

    next_response = expected_response = &response3;
    translate_cache(pool, cache, &request3,
                    &my_translate_handler, nullptr, &async_ref);

    /* now check whether the translate cache matches the BASE
       correctly */

    next_response = nullptr;

    static const TranslateRequest request4 = {
        .uri = "/wfu/a/f/g.html",
    };
    static const TranslateResponse response4 = {
        .address = {
            .type = RESOURCE_ADDRESS_NONE,
        },
        .base = "/wfu/a/",
        .want_full_uri = { "x", 1 },
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
    };

    expected_response = &response4;
    translate_cache(pool, cache, &request4,
                    &my_translate_handler, nullptr, &async_ref);

    static const TranslateRequest request5 = {
        .uri = "/wfu/a/b/0/1.html",
    };

    translate_cache(pool, cache, &request5,
                    &my_translate_handler, nullptr, &async_ref);

    static const TranslateRequest request6 = {
        .uri = "/wfu/a/b/0/1.html",
        .want_full_uri = { "x", 1 },
    };
    static const struct file_address file6("/var/www/vol0/a/b/0/1.html");
    static const TranslateResponse response6 = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .file = &file6,
            },
        },
        .base = "/wfu/a/b/",
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
    };

    expected_response = &response6;
    translate_cache(pool, cache, &request6,
                    &my_translate_handler, nullptr, &async_ref);

    static const TranslateRequest request7 = {
        .uri = "/wfu/a/d/2/3.html",
        .want_full_uri = { "x", 1 },
    };
    static const struct file_address file7("/var/www/vol1/a/d/2/3.html");
    static const TranslateResponse response7 = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .file = &file7,
            },
        },
        .base = "/wfu/a/d/",
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
    };

    expected_response = &response7;
    translate_cache(pool, cache, &request7,
                    &my_translate_handler, nullptr, &async_ref);

    /* expect cache misses */

    expected_response = nullptr;

    static const TranslateRequest miss1 = {
        .uri = "/wfu/a/f/g.html",
        .want_full_uri = { "y", 1 },
    };

    translate_cache(pool, cache, &miss1,
                    &my_translate_handler, nullptr, &async_ref);
}

/**
 * Test UNSAFE_BASE.
 */
static void
test_unsafe_base(struct pool *pool, struct tcache *cache)
{
    struct async_operation_ref async_ref;

    /* feed */
    static constexpr TranslateRequest request1 = {
        .uri = "/unsafe_base1/foo",
    };
    static const struct file_address file1("/var/www/foo");
    static const TranslateResponse response1 = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .file = &file1,
            },
        },
        .base = "/unsafe_base1/",
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
    };

    next_response = expected_response = &response1;
    translate_cache(pool, cache, &request1,
                    &my_translate_handler, nullptr, &async_ref);

    static constexpr TranslateRequest request2 = {
        .uri = "/unsafe_base2/foo",
    };
    static const struct file_address file2("/var/www/foo");
    static const TranslateResponse response2 = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .file = &file2,
            },
        },
        .base = "/unsafe_base2/",
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
        .unsafe_base = true,
    };

    next_response = expected_response = &response2;
    translate_cache(pool, cache, &request2,
                    &my_translate_handler, nullptr, &async_ref);

    /* fail (no UNSAFE_BASE) */

    static constexpr TranslateRequest request3 = {
        .uri = "/unsafe_base1/../x",
    };

    next_response = expected_response = nullptr;
    translate_cache(pool, cache, &request3,
                    &my_translate_handler, nullptr, &async_ref);

    /* success (with UNSAFE_BASE) */

    static constexpr TranslateRequest request4 = {
        .uri = "/unsafe_base2/../x",
    };
    static const struct file_address file4("/var/www/../x");
    static const TranslateResponse response4 = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .file = &file4,
            },
        },
        .base = "/unsafe_base2/",
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
        .unsafe_base = true,
    };

    next_response = nullptr;
    expected_response = &response4;
    translate_cache(pool, cache, &request4,
                    &my_translate_handler, nullptr, &async_ref);
}

/**
 * Test UNSAFE_BASE + EXPAND_PATH.
 */
static void
test_expand_unsafe_base(struct pool *pool, struct tcache *cache)
{
    struct async_operation_ref async_ref;

    /* feed */

    static constexpr TranslateRequest request1 = {
        .uri = "/expand_unsafe_base1/foo",
    };

    static struct file_address file1("/var/www/foo.html");
    file1.expand_path = "/var/www/\\1.html";
    static const TranslateResponse response1 = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .file = &file1,
            },
        },
        .base = "/expand_unsafe_base1/",
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
        .regex = "^/expand_unsafe_base1/(.*)$",
    };

    next_response = expected_response = &response1;
    translate_cache(pool, cache, &request1,
                    &my_translate_handler, nullptr, &async_ref);

    static constexpr TranslateRequest request2 = {
        .uri = "/expand_unsafe_base2/foo",
    };
    static const TranslateResponse response2 = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .file = &file1,
            },
        },
        .base = "/expand_unsafe_base2/",
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
        .regex = "^/expand_unsafe_base2/(.*)$",
        .unsafe_base = true,
    };

    next_response = expected_response = &response2;
    translate_cache(pool, cache, &request2,
                    &my_translate_handler, nullptr, &async_ref);

    /* fail (no UNSAFE_BASE) */

    static constexpr TranslateRequest request3 = {
        .uri = "/expand_unsafe_base1/../x",
    };

    next_response = expected_response = nullptr;
    translate_cache(pool, cache, &request3,
                    &my_translate_handler, nullptr, &async_ref);

    /* success (with UNSAFE_BASE) */

    static constexpr TranslateRequest request4 = {
        .uri = "/expand_unsafe_base2/../x",
    };

    static struct file_address file4("/var/www/../x.html");
    file4.expand_path = "/var/www/\\1.html";
    static const TranslateResponse response4 = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .file = &file4,
            },
        },
        .base = "/expand_unsafe_base2/",
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
        .regex = "^/expand_unsafe_base2/(.*)$",
        .unsafe_base = true,
    };

    next_response = nullptr;
    expected_response = &response4;
    translate_cache(pool, cache, &request4,
                    &my_translate_handler, nullptr, &async_ref);
}

int
main(gcc_unused int argc, gcc_unused char **argv)
{
    tstock *const translate_stock = (tstock *)0x1;
    struct event_base *event_base;
    struct pool *pool;
    struct tcache *cache;

    event_base = event_init();

    pool = pool_new_libc(nullptr, "root");
    tpool_init(pool);

    cache = translate_cache_new(pool, translate_stock, 1024);

    /* test */

    test_basic(pool, cache);
    test_base_root(pool, cache);
    test_base_mismatch(pool, cache);
    test_base_uri(pool, cache);
    test_base_test_path(pool, cache);
    test_easy_base(pool, cache);
    test_easy_base_uri(pool, cache);
    test_easy_base_test_path(pool, cache);
    test_vary_invalidate(pool, cache);
    test_invalidate_uri(pool, cache);
    test_regex(pool, cache);
    test_regex_error(pool, cache);
    test_regex_tail(pool, cache);
    test_regex_tail_unescape(pool, cache);
    test_expand(pool, cache);
    test_expand_local(pool, cache);
    test_expand_local_filter(pool, cache);
    test_expand_uri(pool, cache);
    test_auto_base(pool, cache);
    test_base_check(pool, cache);
    test_base_wfu(pool, cache);
    test_unsafe_base(pool, cache);
    test_expand_unsafe_base(pool, cache);

    /* cleanup */

    translate_cache_close(cache);

    tpool_deinit();
    pool_commit();

    pool_unref(pool);
    pool_commit();
    pool_recycler_clear();

    event_base_free(event_base);
}
