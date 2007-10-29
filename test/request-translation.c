#include "translate.h"
#include "config.h"

#include <stdio.h>
#include <event.h>

static void
translate_callback(const struct translate_response *response,
                   void *ctx)
{
    (void)ctx;

    if (response->status != 0)
        printf("status=%d\n", response->status);
    if (response->path != NULL)
        printf("path=%s\n", response->path);
    if (response->content_type != NULL)
        printf("content_type=%s\n", response->content_type);
    if (response->proxy != NULL)
        printf("proxy=%s\n", response->proxy);
    if (response->redirect != NULL)
        printf("redirect=%s\n", response->redirect);
    if (response->filter != NULL)
        printf("filter=%s\n", response->filter);
    if (response->process)
        printf("process=true\n");
    if (response->session != NULL)
        printf("session=%s\n", response->session);
    if (response->user != NULL)
        printf("user=%s\n", response->user);
}

int main(int argc, char **argv) {
    static struct config config = {
        .translation_socket = "/tmp/beng-proxy-translate",
    };
    struct translate_request request = {
        .host = "example.com",
        .uri = "/foo/index.html",
    };
    pool_t pool;

    (void)argc;
    (void)argv;

    event_init();

    pool = pool_new_libc(NULL, "root");

    translate(pool, &config, &request, translate_callback, NULL);

    event_dispatch();
}
