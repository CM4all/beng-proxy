/*
 * Trace parameters for functions.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_TRACE_H
#define __BENG_TRACE_H

#ifdef TRACE

#define TRACE_ARGS_DECL , const char *file, unsigned line
#define TRACE_ARGS_DECL_ , const char *_file, unsigned _line
#define TRACE_ARGS_FWD , file, line
#define TRACE_ARGS , __FILE__, __LINE__
#define TRACE_ARGS_IGNORE { (void)file; (void)line; }
#define TRACE_ARGS_INIT , file(_file), line(_line)

#else

#define TRACE_ARGS_DECL
#define TRACE_ARGS_DECL_
#define TRACE_ARGS_FWD
#define TRACE_ARGS
#define TRACE_ARGS_IGNORE
#define TRACE_ARGS_INIT

#endif

#endif
