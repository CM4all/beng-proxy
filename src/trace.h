// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

/*
 * Trace parameters for functions.
 */

#pragma once

#ifdef ENABLE_TRACE

#define TRACE_ARGS_DECL , const char *file, unsigned line
#define TRACE_ARGS_DECL_ , const char *_file, unsigned _line
#define TRACE_ARGS_DEFAULT , const char *file=__builtin_FILE(), unsigned line=__builtin_LINE()
#define TRACE_ARGS_DEFAULT_ , const char *_file=__builtin_FILE(), unsigned _line=__builtin_LINE()
#define TRACE_ARGS_FWD , file, line
#define TRACE_ARGS_IGNORE { (void)file; (void)line; }
#define TRACE_ARGS_INIT , file(_file), line(_line)
#define TRACE_ARGS_INIT_FROM(src) , file((src).file), line((src).line)

#else

#define TRACE_ARGS_DECL
#define TRACE_ARGS_DECL_
#define TRACE_ARGS_DEFAULT
#define TRACE_ARGS_DEFAULT_
#define TRACE_ARGS_FWD
#define TRACE_ARGS_IGNORE
#define TRACE_ARGS_INIT
#define TRACE_ARGS_INIT_FROM(src)

#endif
