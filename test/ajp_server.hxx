#include "ajp/ajp_protocol.hxx"

#include <http/status.h>

#include <stddef.h>

struct StringMap;

struct ajp_request {
    enum ajp_code code;
    enum ajp_method method;
    const char *uri;
    StringMap *headers;

    uint8_t *body;
    size_t length, requested, received;
};

void
read_ajp_header(struct ajp_header *header);

void
read_ajp_request(struct pool *pool, struct ajp_request *r);

void
read_ajp_request_body_chunk(struct ajp_request *r);

void
read_ajp_end_request_body_chunk(struct ajp_request *r);

void
discard_ajp_request_body(struct ajp_request *r);

void
write_headers(http_status_t status, const StringMap *headers);

void
write_body_chunk(const void *value, size_t length, size_t junk);

void
write_end();
