#include "tcache.hxx"
#include "tstock.hxx"
#include "translate_client.hxx"
#include "translate_request.hxx"
#include "translate_response.hxx"
#include "async.h"
#include "transformation.h"
#include "widget-view.h"
#include "beng-proxy/translation.h"
#include "http_address.h"
#include "file_address.h"
#include "cgi_address.h"
#include "pool.h"

#include <event.h>

#include <string.h>

const TranslateResponse *next_response, *expected_response;

void
tstock_translate(gcc_unused struct tstock *stock, gcc_unused struct pool *pool,
                 gcc_unused const TranslateRequest *request,
                 const TranslateHandler *handler, void *ctx,
                 gcc_unused struct async_operation_ref *async_ref)
{
    if (next_response != nullptr)
        handler->response(next_response, ctx);
    else
        handler->error(g_error_new(translate_quark(), 0, "Error"), ctx);
}

static bool
string_equals(const char *a, const char *b)
{
    if (a == nullptr || b == nullptr)
        return a == nullptr && b == nullptr;

    return strcmp(a, b) == 0;
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
transformation_equals(const struct transformation *a,
                      const struct transformation *b)
{
    assert(a != nullptr);
    assert(b != nullptr);

    if (a->type != b->type)
        return false;

    switch (a->type) {
    case transformation::TRANSFORMATION_PROCESS:
        return a->u.processor.options == b->u.processor.options;

    case transformation::TRANSFORMATION_PROCESS_CSS:
        return a->u.css_processor.options == b->u.css_processor.options;

    case transformation::TRANSFORMATION_PROCESS_TEXT:
        return true;

    case transformation::TRANSFORMATION_FILTER:
        return resource_address_equals(&a->u.filter, &b->u.filter);
    }

    /* unreachable */
    assert(false);
    return false;
}

static bool
transformation_chain_equals(const struct transformation *a,
                  const struct transformation *b)
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
view_equals(const struct widget_view *a, const struct widget_view *b)
{
    assert(a != nullptr);
    assert(b != nullptr);

    return string_equals(a->name, b->name) &&
        resource_address_equals(&a->address, &b->address) &&
        a->filter_4xx == b->filter_4xx &&
        transformation_chain_equals(a->transformation, b->transformation);
}

static bool
view_chain_equals(const struct widget_view *a,
                  const struct widget_view *b)
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
        a->easy_base == b->easy_base &&
        a->unsafe_base == b->unsafe_base &&
        strref_is_null(&a->check) == strref_is_null(&b->check) &&
        strref_cmp2(&a->check, &b->check) == 0 &&
        strref_is_null(&a->want_full_uri) == strref_is_null(&b->want_full_uri) &&
        strref_cmp2(&a->want_full_uri, &b->want_full_uri) == 0 &&
        resource_address_equals(&a->address, &b->address) &&
        view_chain_equals(a->views, b->views);
}

static void
my_translate_response(const TranslateResponse *response,
                      gcc_unused void *ctx)
{
    assert(translate_response_equals(response, expected_response));
}

static void
my_translate_error(GError *error, G_GNUC_UNUSED void *ctx)
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

    static const struct file_address file1 = {
        .path = "/var/www/index.html",
    };
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

    static const struct file_address file2 = {
        .path = "/srv/foo/bar.html",
    };
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

    static const struct file_address file3 = {
        .path = "/srv/foo/index.html",
    };
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

    static const struct file_address file4 = {
        .path = "/srv/foo/",
    };
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
    static const struct file_address file10 = {
        .path = "/srv/foo//bar",
    };
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

static void
test_easy_base(struct pool *pool, struct tcache *cache)
{
    static const TranslateRequest request1 = {
        .uri = "/easy/bar.html",
    };
    static const TranslateRequest request2 = {
        .uri = "/easy/index.html",
    };

    static const struct file_address file1 = {
        .path = "/var/www/",
    };
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

    static const struct file_address file1b = {
        .path = "/var/www/bar.html",
    };
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

    static const struct file_address file2 = {
        .path = "/var/www/index.html",
    };
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

    static const struct file_address file5a = {
        .path = "/srv/qs1",
    };
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

    static const struct file_address file5b = {
        .path = "/srv/qs2",
    };
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

    static const struct file_address file5c = {
        .path = "/srv/qs3",
    };
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
    static const struct file_address file1 = {
        .path = "/var/www/invalidate/uri",
    };
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
        .check = {
            .length = 1,
            .data = "x",
        },
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
    static const struct file_address file3 = {
        .path = "/var/www/500/invalidate/uri",
    };
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
        .check = {
            .length = 1,
            .data = "x",
        },
    };
    static const struct file_address file4 = {
        .path = "/var/www/500/check/invalidate/uri",
    };
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
        .check = {
            .length = 1,
            .data = "x",
        },
        .want_full_uri = {
            .length = 4,
            .data = "a\0/b",
        },
    };
    static const struct file_address file4b = {
        .path = "/var/www/500/check/wfu/invalidate/uri",
    };
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
    static const struct file_address file5 = {
        .path = "/var/www/404/invalidate/uri",
    };
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
    static const struct file_address file_i1 = {
        .path = "/var/www/regex/other/foo",
    };
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
    static const struct file_address file_i2 = {
        .path = "/var/www/regex/other/bar",
    };
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
    static const struct file_address file1 = {
        .path = "/var/www/regex/images/a/foo.jpg",
    };
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
    static const struct file_address file2 = {
        .path = "/var/www/regex/html/b/foo.html",
    };
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
    static const struct file_address file3 = {
        .path = "/var/www/regex/images/c/bar.jpg",
    };
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
    static const struct file_address file4 = {
        .path = "/var/www/regex/html/d/bar.html",
    };
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
test_regex_tail(struct pool *pool, struct tcache *cache)
{
    struct async_operation_ref async_ref;

    static const TranslateRequest request1 = {
        .uri = "/regex_tail/a/foo.jpg",
    };
    static const struct file_address file1 = {
        .path = "/var/www/regex/images/a/foo.jpg",
    };
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
    static const struct file_address file3 = {
        .path = "/var/www/regex/images/a/bar.jpg",
    };
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
    static const struct file_address file1n = {
        .path = "/dummy",
        .expand_path = "/var/www/\\1",
    };
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

    static const struct file_address file1e = {
        .path = "/var/www/foo/bar.jpg",
        .expand_path = "/var/www/\\1",
    };
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
    static const struct file_address file2 = {
        .path = "/var/www/x/y/z.jpg",
    };
    static const TranslateResponse response2 = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .file = &file2,
            },
        },
        .base = "/regex-expand2/",
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
    static struct transformation transformation1n = {
        .type = transformation::TRANSFORMATION_FILTER,
        .u.filter = {
            .type = RESOURCE_ADDRESS_CGI,
            .u = {
                .cgi = &cgi1n,
            },
        },
    };
    static struct widget_view view1n = {
        .transformation = &transformation1n,
    };
    static const struct file_address file1n = {
        .path = "/dummy",
        .expand_path = "/var/www/\\1",
    };
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
    static struct transformation transformation1e = {
        .type = transformation::TRANSFORMATION_FILTER,
        .u.filter = {
            .type = RESOURCE_ADDRESS_CGI,
            .u = {
                .cgi = &cgi1e,
            },
        },
    };
    static struct widget_view view1e = {
        .transformation = &transformation1e,
    };
    static const struct file_address file1e = {
        .path = "/var/www/foo/bar.jpg",
        .expand_path = "/var/www/\\1",
    };
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
    static struct transformation transformation2 = {
        .type = transformation::TRANSFORMATION_FILTER,
        .u.filter = {
            .type = RESOURCE_ADDRESS_CGI,
            .u = {
                .cgi = &cgi2,
            },
        },
    };
    static struct widget_view view2 = {
        .transformation = &transformation2,
    };
    static const struct file_address file2 = {
        .path = "/var/www/x/y/z.jpg",
    };
    static const TranslateResponse response2 = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .file = &file2,
            },
        },
        .base = "/regex-expand3/",
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
        .check = {
            .length = 1,
            .data = "x",
        },
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
    };

    next_response = expected_response = &response1;
    translate_cache(pool, cache, &request1,
                    &my_translate_handler, nullptr, &async_ref);

    static const TranslateRequest request2 = {
        .uri = "/a/b/c.html",
        .check = {
            .length = 1,
            .data = "x",
        },
    };
    static const struct file_address file2 = {
        .path = "/var/www/vol0/a/b/c.html",
    };
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
        .check = {
            .length = 1,
            .data = "x",
        },
    };
    static const struct file_address file3 = {
        .path = "/var/www/vol1/a/d/e.html",
    };
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
        .check = {
            .length = 1,
            .data = "x",
        },
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
        .check = {
            .length = 1,
            .data = "x",
        },
    };
    static const struct file_address file6 = {
        .path = "/var/www/vol0/a/b/0/1.html",
    };
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
        .check = {
            .length = 1,
            .data = "x",
        },
    };
    static const struct file_address file7 = {
        .path = "/var/www/vol1/a/d/2/3.html",
    };
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
        .check = {
            .length = 1,
            .data = "y",
        },
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
        .want_full_uri = {
            .length = 1,
            .data = "x",
        },
        .max_age = unsigned(-1),
        .user_max_age = unsigned(-1),
    };

    next_response = expected_response = &response1;
    translate_cache(pool, cache, &request1,
                    &my_translate_handler, nullptr, &async_ref);

    static const TranslateRequest request2 = {
        .uri = "/wfu/a/b/c.html",
        .want_full_uri = {
            .length = 1,
            .data = "x",
        },
    };
    static const struct file_address file2 = {
        .path = "/var/www/vol0/a/b/c.html",
    };
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
        .want_full_uri = {
            .length = 1,
            .data = "x",
        },
    };
    static const struct file_address file3 = {
        .path = "/var/www/vol1/a/d/e.html",
    };
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
        .want_full_uri = {
            .length = 1,
            .data = "x",
        },
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
        .want_full_uri = {
            .length = 1,
            .data = "x",
        },
    };
    static const struct file_address file6 = {
        .path = "/var/www/vol0/a/b/0/1.html",
    };
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
        .want_full_uri = {
            .length = 1,
            .data = "x",
        },
    };
    static const struct file_address file7 = {
        .path = "/var/www/vol1/a/d/2/3.html",
    };
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
        .want_full_uri = {
            .length = 1,
            .data = "y",
        },
    };

    translate_cache(pool, cache, &miss1,
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

    cache = translate_cache_new(pool, translate_stock, 1024);

    /* test */

    test_basic(pool, cache);
    test_easy_base(pool, cache);
    test_vary_invalidate(pool, cache);
    test_invalidate_uri(pool, cache);
    test_regex(pool, cache);
    test_regex_tail(pool, cache);
    test_expand(pool, cache);
    test_expand_local(pool, cache);
    test_expand_local_filter(pool, cache);
    test_expand_uri(pool, cache);
    test_auto_base(pool, cache);
    test_base_check(pool, cache);
    test_base_wfu(pool, cache);

    /* cleanup */

    translate_cache_close(cache);

    pool_unref(pool);
    pool_commit();
    pool_recycler_clear();

    event_base_free(event_base);
}
