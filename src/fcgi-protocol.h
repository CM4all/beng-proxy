/*
 * FastCGI client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_FCGI_PROTOCOL_H
#define __BENG_FCGI_PROTOCOL_H

#include <inline/compiler.h>

#include <stdint.h>

#define FCGI_VERSION_1 1

#define FCGI_BEGIN_REQUEST       1
#define FCGI_ABORT_REQUEST       2
#define FCGI_END_REQUEST         3
#define FCGI_PARAMS              4
#define FCGI_STDIN               5
#define FCGI_STDOUT              6
#define FCGI_STDERR              7
#define FCGI_DATA                8
#define FCGI_GET_VALUES          9
#define FCGI_GET_VALUES_RESULT  10
#define FCGI_UNKNOWN_TYPE       11
#define FCGI_MAXTYPE (FCGI_UNKNOWN_TYPE)

/*
 * Mask for flags component of FCGI_BeginRequestBody
 */
#define FCGI_KEEP_CONN  1

/*
 * Values for role component of FCGI_BeginRequestBody
 */
#define FCGI_RESPONDER  1
#define FCGI_AUTHORIZER 2
#define FCGI_FILTER     3

struct fcgi_record_header {
    unsigned char version;
    unsigned char type;
    uint16_t request_id;
    uint16_t content_length;
    unsigned char padding_length;
    unsigned char reserved;
    /*
    unsigned char content_data[content_length];
    unsigned char padding_data[padding_length];
    */
} __attr_packed;

struct fcgi_begin_request {
    uint16_t role;
    unsigned char flags;
    unsigned char reserved[5];
} __attr_packed;

#endif
