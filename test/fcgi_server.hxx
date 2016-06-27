#include "fcgi/Protocol.hxx"

#include <http/method.h>
#include <http/status.h>

#include <sys/types.h>
#include <string.h>

struct pool;
class StringMap;
struct fcgi_record_header;

struct FcgiRequest {
    uint16_t id;

    http_method_t method;
    const char *uri;
    StringMap *headers;

    off_t length;
};

void
read_fcgi_header(struct fcgi_record_header *header);

void
read_fcgi_request(struct pool *pool, FcgiRequest *r);

void
discard_fcgi_request_body(FcgiRequest *r);

void
write_fcgi_stdout(const FcgiRequest *r,
                  const void *data, size_t length);

static inline void
write_fcgi_stdout_string(const FcgiRequest *r,
                         const char *data)
{
    write_fcgi_stdout(r, data, strlen(data));
}

void
write_fcgi_headers(const FcgiRequest *r, http_status_t status,
                   StringMap *headers);

void
write_fcgi_end(const FcgiRequest *r);
