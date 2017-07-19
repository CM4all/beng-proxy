/*
 * Run a CGI script.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "cgi_launch.hxx"
#include "cgi_address.hxx"
#include "istream/istream.hxx"
#include "strmap.hxx"
#include "product.h"
#include "spawn/IstreamSpawn.hxx"
#include "spawn/JailParams.hxx"
#include "spawn/Prepared.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "util/CharUtil.hxx"

#include <sys/wait.h>
#include <string.h>

static const char *
cgi_address_name(const CgiAddress *address)
{
    if (address->interpreter != nullptr)
        return address->interpreter;

    if (address->action != nullptr)
        return address->action;

    if (address->path != nullptr)
        return address->path;

    return "CGI";
}

static constexpr const char *
StringFallback(const char *value, const char *fallback)
{
    return value != nullptr ? value : fallback;
}

/**
 * Throws std::runtime_error on error.
 */
static void
PrepareCgi(struct pool &pool, PreparedChildProcess &p,
           http_method_t method,
           const CgiAddress &address,
           const char *remote_addr,
           const StringMap &headers,
           off_t content_length)
{
    const char *path = address.path;

    p.SetEnv("GATEWAY_INTERFACE", "CGI/1.1");
    p.SetEnv("SERVER_PROTOCOL", "HTTP/1.1");
    p.SetEnv("REQUEST_METHOD", http_method_to_string(method));
    p.SetEnv("SCRIPT_FILENAME", path);
    p.SetEnv("PATH_TRANSLATED", path);
    p.SetEnv("REQUEST_URI", address.GetURI(&pool));
    p.SetEnv("SCRIPT_NAME", StringFallback(address.script_name, ""));
    p.SetEnv("PATH_INFO", StringFallback(address.path_info, ""));
    p.SetEnv("QUERY_STRING", StringFallback(address.query_string, ""));
    p.SetEnv("DOCUMENT_ROOT",
             StringFallback(address.document_root, "/var/www"));
    p.SetEnv("SERVER_SOFTWARE", PRODUCT_TOKEN);

    if (remote_addr != nullptr)
        p.SetEnv("REMOTE_ADDR", remote_addr);

    const char *arg = nullptr;
    if (address.options.jail != nullptr && address.options.jail->enabled) {
        p.SetEnv("JAILCGI_FILENAME", path);
        path = "/usr/lib/cm4all/jailcgi/bin/wrapper";

        if (address.options.jail->home_directory != nullptr)
            p.SetEnv("JETSERV_HOME", address.options.jail->home_directory);

        if (address.interpreter != nullptr)
            p.SetEnv("JAILCGI_INTERPRETER", address.interpreter);

        if (address.action != nullptr)
            p.SetEnv("JAILCGI_ACTION", address.action);
    } else {
        if (address.action != nullptr)
            path = address.action;

        if (address.interpreter != nullptr) {
            arg = path;
            path = address.interpreter;
        }
    }

    const char *content_type = nullptr;
    for (const auto &pair : headers) {
        if (strcmp(pair.key, "content-type") == 0) {
            content_type = pair.value;
            continue;
        }

        if (strcmp(pair.key, "proxy") == 0)
            /* work around vulnerability in several CGI programs
               which take the environment variable HTTP_PROXY as
               proxy specification for their internal HTTP
               clients; see CVE-2016-5385 and others */
            continue;

        if (strcmp(pair.key, "x-cm4all-https") == 0)
            /* this will be translated to HTTPS */
            continue;

        char buffer[512] = "HTTP_";
        size_t i;
        for (i = 0; 5 + i < sizeof(buffer) - 1 && pair.key[i] != 0; ++i) {
            if (IsLowerAlphaASCII(pair.key[i]))
                buffer[5 + i] = (char)(pair.key[i] - 'a' + 'A');
            else if (IsUpperAlphaASCII(pair.key[i]) ||
                     IsDigitASCII(pair.key[i]))
                buffer[5 + i] = pair.key[i];
            else
                buffer[5 + i] = '_';
        }

        buffer[5 + i] = 0;
        p.SetEnv(buffer, pair.value);
    }

    if (content_type != nullptr)
        p.SetEnv("CONTENT_TYPE", content_type);

    if (content_length >= 0) {
        char value[32];
        snprintf(value, sizeof(value), "%llu",
                 (unsigned long long)content_length);
        p.SetEnv("CONTENT_LENGTH", value);
    }

    const char *https = headers.Get("x-cm4all-https");
    if (https != nullptr && strcmp(https, "on") == 0)
        p.SetEnv("HTTPS", "on");

    p.Append(path);
    for (auto i : address.args)
        p.Append(i);
    if (arg != nullptr)
        p.Append(arg);

    address.options.CopyTo(p, false, nullptr);
}

Istream *
cgi_launch(EventLoop &event_loop, struct pool *pool,
           http_method_t method,
           const CgiAddress *address,
           const char *remote_addr,
           const StringMap &headers, Istream *body,
           SpawnService &spawn_service)
{
    PreparedChildProcess p;
    PrepareCgi(*pool, p, method,
               *address, remote_addr, headers,
               body != nullptr ? body->GetAvailable(false) : -1);

    Istream *input;
    SpawnChildProcess(event_loop, pool,
                      cgi_address_name(address), body, &input,
                      std::move(p),
                      spawn_service);
    return input;
}
