/*
 * Compiler specific macros.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_COMPILER_H
#define __BENG_COMPILER_H

#undef inline

#if defined(__GNUC__) && __GNUC__ >= 4

#define inline inline __attribute__((always_inline))

#else
#endif

#endif
