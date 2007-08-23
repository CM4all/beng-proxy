/*
 * Compiler specific macros.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_COMPILER_H
#define __BENG_COMPILER_H

#undef inline

#if defined(__GNUC__) && __GNUC__ >= 4

#define always_inline inline __attribute__((always_inline))

#ifdef ALWAYS_INLINE
#define inline inline __attribute__((always_inline))
#endif

#define attr_malloc __attribute__((malloc))
#define attr_pure __attribute__((pure))
#define attr_const __attribute__((const))
#define attr_printf(string_index, first_to_check) __attribute__((format(printf, string_index, first_to_check)))

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#else

#define always_inline inline

#define attr_malloc
#define attr_pure
#define attr_const
#define attr_printf(string_index, first_to_check)

#define likely(x)	(x)
#define unlikely(x)	(x)

#endif

#endif
