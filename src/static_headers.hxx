/*
 * Handle the request/response headers for static files.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_STATIC_HEADERS_HXX
#define BENG_PROXY_STATIC_HEADERS_HXX

#include <sys/stat.h>
#include <stddef.h>

struct pool;
struct strmap;
struct stat;
struct file_request;

void
static_etag(char *p, const struct stat *st);

bool
load_xattr_content_type(char *buffer, size_t size, int fd);

/**
 * @param fd a file descriptor for loading xattr, or -1 to disable
 * xattr
 */
void
static_response_headers(struct pool *pool, struct strmap *headers,
                        int fd, const struct stat *st,
                        const char *content_type);

#endif
