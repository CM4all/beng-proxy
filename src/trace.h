/*
 * Trace parameters for functions.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_TRACE_H
#define __BENG_TRACE_H

#ifdef TRACE

#define TRACE_ARGS_DECL , const char *file, unsigned line
#define TRACE_ARGS_FWD , file, line
#define TRACE_ARGS , __FILE__, __LINE__
#define TRACE_ARGS_IGNORE { (void)file; (void)line; }

#else

#define TRACE_ARGS_DECL
#define TRACE_ARGS_FWD
#define TRACE_ARGS
#define TRACE_ARGS_IGNORE

#endif

#endif
