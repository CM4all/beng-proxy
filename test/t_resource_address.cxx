#include "tconstruct.hxx"
#include "ResourceAddress.hxx"
#include "file_address.hxx"
#include "cgi_address.hxx"
#include "pool.hxx"
#include "RootPool.hxx"

#include <assert.h>
#include <string.h>

static void
test_auto_base(struct pool *pool)
{
    static const auto cgi0 =
        MakeCgiAddress("/usr/lib/cgi-bin/foo.pl", nullptr, "/");
    static constexpr ResourceAddress ra0(ResourceAddress::Type::CGI, cgi0);

    assert(ra0.AutoBase(*pool, "/") == NULL);
    assert(ra0.AutoBase(*pool, "/foo") == NULL);

    static const auto cgi1 =
        MakeCgiAddress("/usr/lib/cgi-bin/foo.pl", nullptr, "foo/bar");
    static constexpr ResourceAddress ra1(ResourceAddress::Type::CGI, cgi1);

    assert(ra1.AutoBase(*pool, "/") == NULL);
    assert(ra1.AutoBase(*pool, "/foo/bar") == NULL);

    static const auto cgi2 =
        MakeCgiAddress("/usr/lib/cgi-bin/foo.pl", nullptr, "/bar/baz");
    static constexpr ResourceAddress ra2(ResourceAddress::Type::CGI, cgi2);

    assert(ra2.AutoBase(*pool, "/") == NULL);
    assert(ra2.AutoBase(*pool, "/foobar/baz") == NULL);

    char *a = ra2.AutoBase(*pool, "/foo/bar/baz");
    assert(a != NULL);
    assert(strcmp(a, "/foo/") == 0);
}

static void
test_base_no_path_info(struct pool *pool)
{
    static const auto cgi0 = MakeCgiAddress("/usr/lib/cgi-bin/foo.pl");
    static constexpr ResourceAddress ra0(ResourceAddress::Type::CGI, cgi0);

    ResourceAddress dest, *b;

    b = ra0.SaveBase(*pool, dest, "");
    assert(b != nullptr);
    assert(b->type == ResourceAddress::Type::CGI);
    assert(strcmp(b->GetCgi().path, ra0.GetCgi().path) == 0);
    assert(b->GetCgi().path_info == nullptr ||
           strcmp(b->GetCgi().path_info, "") == 0);

    b = ra0.LoadBase(*pool, dest, "foo/bar");
    assert(b != nullptr);
    assert(b->type == ResourceAddress::Type::CGI);
    assert(strcmp(b->GetCgi().path, ra0.GetCgi().path) == 0);
    assert(strcmp(b->GetCgi().path_info, "foo/bar") == 0);
}

static void
test_cgi_apply(struct pool *pool)
{
    static const auto cgi0 =
        MakeCgiAddress("/usr/lib/cgi-bin/foo.pl", nullptr, "/foo/");
    static constexpr ResourceAddress ra0(ResourceAddress::Type::CGI, cgi0);

    auto result = ra0.Apply(*pool, "");
    assert(&result.GetCgi() == &ra0.GetCgi());

    result = ra0.Apply(*pool, "bar");
    assert(strcmp(result.GetCgi().path_info, "/foo/bar") == 0);

    result = ra0.Apply(*pool, "/bar");
    assert(strcmp(result.GetCgi().path_info, "/bar") == 0);

    /* PATH_INFO is unescaped (RFC 3875 4.1.5) */
    result = ra0.Apply(*pool, "bar%2etxt");
    assert(strcmp(result.GetCgi().path_info, "/foo/bar.txt") == 0);

    result = ra0.Apply(*pool, "http://localhost/");
    assert(!result.IsDefined());
}

/*
 * main
 *
 */

int main(int argc, char **argv) {
    static const FileAddress file1("/var/www/foo/bar.html");
    static constexpr ResourceAddress ra1(file1);

    static const FileAddress file2("/var/www/foo/space .txt");
    static constexpr ResourceAddress ra2(file2);

    static const auto cgi3 =
        MakeCgiAddress("/usr/lib/cgi-bin/foo.pl",
                       "/foo/bar/baz",
                       "/bar/baz");
    static constexpr ResourceAddress ra3(ResourceAddress::Type::CGI, cgi3);

    ResourceAddress *a, *b, dest, dest2;

    (void)argc;
    (void)argv;

    RootPool pool;

    a = ra1.SaveBase(*pool, dest2, "bar.html");
    assert(a != NULL);
    assert(a->type == ResourceAddress::Type::LOCAL);
    assert(strcmp(a->GetFile().path, "/var/www/foo/") == 0);

    b = a->LoadBase(*pool, dest, "index.html");
    assert(b != NULL);
    assert(b->type == ResourceAddress::Type::LOCAL);
    assert(strcmp(b->GetFile().path, "/var/www/foo/index.html") == 0);

    a = ra2.SaveBase(*pool, dest2, "space%20.txt");
    assert(a != NULL);
    assert(a->type == ResourceAddress::Type::LOCAL);
    assert(strcmp(a->GetFile().path, "/var/www/foo/") == 0);

    b = a->LoadBase(*pool, dest, "index%2ehtml");
    assert(b != NULL);
    assert(b->type == ResourceAddress::Type::LOCAL);
    assert(strcmp(b->GetFile().path, "/var/www/foo/index.html") == 0);

    a = ra3.SaveBase(*pool, dest2, "bar/baz");
    assert(a != NULL);
    assert(a->type == ResourceAddress::Type::CGI);
    assert(strcmp(a->GetCgi().path, ra3.GetCgi().path) == 0);
    assert(strcmp(a->GetCgi().path_info, "/") == 0);

    b = a->LoadBase(*pool, dest, "");
    assert(b != NULL);
    assert(b->type == ResourceAddress::Type::CGI);
    assert(strcmp(b->GetCgi().path, ra3.GetCgi().path) == 0);
    assert(strcmp(b->GetCgi().uri, "/foo/") == 0);
    assert(strcmp(b->GetCgi().path_info, "/") == 0);

    b = a->LoadBase(*pool, dest, "xyz");
    assert(b != NULL);
    assert(b->type == ResourceAddress::Type::CGI);
    assert(strcmp(b->GetCgi().path, ra3.GetCgi().path) == 0);
    assert(strcmp(b->GetCgi().uri, "/foo/xyz") == 0);
    assert(strcmp(b->GetCgi().path_info, "/xyz") == 0);

    a = ra3.SaveBase(*pool, dest2, "baz");
    assert(a != NULL);
    assert(a->type == ResourceAddress::Type::CGI);
    assert(strcmp(a->GetCgi().path, ra3.GetCgi().path) == 0);
    assert(strcmp(a->GetCgi().uri, "/foo/bar/") == 0);
    assert(strcmp(a->GetCgi().path_info, "/bar/") == 0);

    b = a->LoadBase(*pool, dest, "bar/");
    assert(b != NULL);
    assert(b->type == ResourceAddress::Type::CGI);
    assert(strcmp(b->GetCgi().path, ra3.GetCgi().path) == 0);
    assert(strcmp(b->GetCgi().uri, "/foo/bar/bar/") == 0);
    assert(strcmp(b->GetCgi().path_info, "/bar/bar/") == 0);

    b = a->LoadBase(*pool, dest, "bar/xyz");
    assert(b != NULL);
    assert(b->type == ResourceAddress::Type::CGI);
    assert(strcmp(b->GetCgi().path, ra3.GetCgi().path) == 0);
    assert(strcmp(b->GetCgi().uri, "/foo/bar/bar/xyz") == 0);
    assert(strcmp(b->GetCgi().path_info, "/bar/bar/xyz") == 0);

    test_auto_base(pool);
    test_base_no_path_info(pool);
    test_cgi_apply(pool);
}
