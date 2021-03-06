#ifndef __MSTRING_TYPES_H__
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * (c) Copyright 2006,2007,2008 MString Core Team <http://mstring.jarios.org>
 *
 */

#define __MSTRING_TYPES_H__

#include <config.h>
#include <arch/types.h>

#define NULL ((void *)0)
#define true  1
#define false 0

typedef int32_t pid_t;
typedef uint64_t tid_t;
typedef unsigned char uchar_t;
typedef unsigned int uint_t;
typedef unsigned long ulong_t;
typedef uint_t bool;
typedef ulong_t size_t;
typedef uint16_t uid_t;
typedef uint16_t gid_t;
typedef uint32_t mode_t;
typedef int cpu_id_t;
typedef unsigned long cpumask_t;
typedef uint32_t irq_t;
typedef uint64_t tick_t;
typedef unsigned long lock_t;

#define always_inline __attribute__((always_inline)) inline
#define likely(x)   __builtin_expect((x), 1)
#define unlikely(x) __builtin_expect((x), 0)
#define mbarrier()  __asm__ volatile("")
#define INITDATA __attribute__ ((section("initdata")))
#define INITCODE __attribute__ ((section("initcode")))
#define __aligned(x) __attribute__ ((aligned(x)))

#ifndef UNUSED
#define UNUSED(instr) ((void)(instr))
#endif /* UNUSED */

#ifdef CONFIG_TRACE_ERRORS

#define ERR(err)                                                      \
  ({  if( (err) < 0 ) {                                               \
      kprintf_fault("ERR(%d) in module %s, function %s, line %d\n",   \
        (err),__FILE__,__FUNCTION__,__LINE__);                        \
     }                                                                \
    (err); })

#else
  #define ERR(err) err
#endif

#endif /* __MSTRING_TYPES_H__ */
