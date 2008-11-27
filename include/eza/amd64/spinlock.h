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
 * (c) Copyright 2006,2007,2008 MString Core Team <http://mstring.berlios.de>
 * (c) Copyright 2008 Michael Tsymbalyuk <mtzaurus@gmail.com>
 * (c) Copyright 2008 Dan Kruchinin <dan.kruchinin@gmail.com>
 *
 * include/eza/amd64/spinlock.h: spinlock and atomic amd64 specific and 
 *                               extended functions
 *
 */

#ifndef __AMD64_SPINLOCK_H__
#define __AMD64_SPINLOCK_H__

#include <config.h>
#include <eza/arch/types.h>
#include <eza/arch/mbarrier.h>
#include <eza/arch/bitwise.h>
#include <eza/arch/asm.h>

#define __SPINLOCK_LOCKED_V   1
#define __SPINLOCK_UNLOCKED_V 0

#ifdef CONFIG_SMP
typedef struct __spinlock_type {
    long_t __spin_val;
} spinlock_t;

typedef struct __rw_spinlock_type {
    lock_t __r, __w;
} rw_spinlock_t;


#define arch_spinlock_lock_bit(bitmap, bit)     \
  while (arch_bit_test_and_set(bitmap, bit))

#define arch_spinlock_unlock_bit(bitmap, bit)   \
  arch_bit_clear(bitmap, bit)

static always_inline void arch_spinlock_lock(spinlock_t *lock)
{
  __asm__ __volatile__(  "movl %2,%%eax\n"
                         "1:" __LOCK_PREFIX "cmpxchgl %0, %1\n"
                         "cmp %2, %%eax\n"
                         "jnz 1b\n"
                         :: "r"(__SPINLOCK_LOCKED_V),"m"(lock->__spin_val), "rI"(__SPINLOCK_UNLOCKED_V)
                         : "%rax", "memory" );
}

static always_inline void arch_spinlock_unlock(spinlock_t *lock)
{
  __asm__ __volatile__( __LOCK_PREFIX "xchgl %0, %1\n"
                        :: "r"(__SPINLOCK_UNLOCKED_V), "m"( lock->__spin_val )
                        : "memory" );
}

static always_inline bool arch_spinlock_trylock(spinlock_t *lock)
{
  int ret = __SPINLOCK_UNLOCKED_V;
  __asm__ volatile (__LOCK_PREFIX "cmpxchgl %2, %0\n\t"
                    : "+m" (lock->__spin_val), "=&a" (ret)
                    : "Ir" (__SPINLOCK_LOCKED_V));

  return !ret;
}

static always_inline bool arch_spinlock_is_locked(spinlock_t *lock)
{
  return (lock->__spin_val == __SPINLOCK_LOCKED_V);
}

/* Main strategy of JariOS read-write spinlocks.
 * We use two counters: one for counting readers and one for counting
 * writers. There can be only 0 or 1 writers, but unlimited amount of
 * readers, so we can use bits [1 .. N] of the 'writers' counter for
 * our needs. We deal with RW spinlocks in two steps. First, we implement
 * a pure spinlock (to access these two variables atomically) via 'btr'
 * instruction. After we've grabbed the first lock, we compare its value
 * against 256, since we use 8th bit of the 'writers' counter - since
 * there can be only 0 or 1 writers, we will always get either 256 or
 * 257 in this counter after setting 8th bit (after locking the lock).
 * So if we have 257, this means that our RW lock has been grabbed
 * by a writer and we must release the bit and repeat the procedure
 * from the beginning waiting for the writer to decrement the counter.
 */

static always_inline void arch_spinlock_lock_read(rw_spinlock_t *lock)
{
  __asm__ __volatile__( "0: movq %0,%%rax\n"
                        "1:" __LOCK_PREFIX "bts %%rax,%1\n"
                        "jc 1b\n"
                        "cmpl $256, %1\n"
                        "je 3f\n"
                        __LOCK_PREFIX "btr %%rax,%1\n"
                        "jmp 0b\n"
                        "3: " __LOCK_PREFIX "incl %2\n"
                        "4: " __LOCK_PREFIX "btr\r %%rax,%1\n"
                        :: "rI"(8), "m"(lock->__w),
                        "m"(lock->__r) : "%rax", "memory" );
}

static always_inline void arch_spinlock_lock_write(rw_spinlock_t *lock)
{
  __asm__ __volatile__( "0: movq %0,%%rax\n"
                        "1:" __LOCK_PREFIX "bts %%rax,%1\n"
                        "jc 1b\n"
                        "cmpl $256, %1\n"
                        "je 3f\n"
                        "2:" __LOCK_PREFIX "btr %%rax,%1\n"
                        "jmp 0b\n"
                        "3: cmpl $0, %2\n"
                        "jne 2b\n"
                        __LOCK_PREFIX "incl %1\n"
                        __LOCK_PREFIX "btr\r %%rax,%1\n"
                        :: "rI"(8), "m"(lock->__w),
                        "m"(lock->__r) : "%rax", "memory" );  
}

static always_inline void arch_spinlock_unlock_read(rw_spinlock_t *lock)
{
  __asm__ __volatile__( __LOCK_PREFIX "decl %0\n"
                        :: "m"(lock->__r)
                        : "memory" );
}

static always_inline void arch_spinlock_unlock_write(rw_spinlock_t *lock)
{
  __asm__ __volatile__( __LOCK_PREFIX "decl %0\n"
                        :: "m"(lock->__w)
                        : "memory" );
}

/* TODO DK: implement trylock and is_locked form RW spinlocks */
#else /* !CONFIG_SMP */
typedef int spinlock_t;
typedef int rw_spinlock_t;
#endif /* CONFIG_SMP */

#endif /* __AMD64_SPINLOCK_H__ */

