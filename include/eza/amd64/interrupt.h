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
 * (c) Copyright 2008 Michael Tsymbalyuk <mtzaurus@gmail.com>
 *
 * include/eza/amd64/interrupt.h: AMD64-specific interrupt processing logic
 *                                constants, types and inlines.
 */

#ifndef __AMD64_INTERRUPT_H__
#define __AMD64_INTERRUPT_H__ 

#include <eza/arch/asm.h>
#include <eza/arch/types.h>
#include <eza/arch/current.h>

#define HZ  1000 /* Timer frequency. */
#define IRQ_BASE 32 /* First vector in IDT for IRQ #0. */
#define RESERVED_IRQS 8 /* Reserved IRQ for SMP use. */
/* Maximum number of hardware IRQs in the system. */
#define NUM_IRQS  256 - IRQ_BASE - RESERVED_IRQS

//#ifdef CONFIG_SMP

#define CPU_SMP_BASE_IRQ (256 - RESERVED_IRQS)
#define LOCAL_TIMER_CPU_IRQ_VEC CPU_SMP_BASE_IRQ
#define SCHEDULER_IPI_IRQ_VEC (CPU_SMP_BASE_IRQ+1)

//#endif

/* AMD 64 interrupt/exception stack frame */
typedef struct __interrupt_stack_frame {
  uint64_t rip, cs;
  uint64_t rflags, old_rsp, old_ss;
} interrupt_stack_frame_t;

typedef struct __interrupt_stack_frame_err {
  uint64_t error_code;
  uint64_t rip, cs;
  uint64_t rflags, old_rsp, old_ss;
} interrupt_stack_frame_err_t;

#define saved_gprs_from_err_stackframe(s)       \
  (struct __gpr_regs *)(((uintptr_t)(s))-sizeof(struct __gpr_regs)-8)

extern volatile cpu_id_t online_cpus;

/* interrupts_enable(): enabling interrupts and return
 * previous EFLAGS value.
 */
static inline ipl_t interrupts_enable(void)
{
  ipl_t o;

  __asm__ volatile (
                    "pushfq\n"
                    "popq %0\n"
                    "sti\n"
                    : "=r" (o)
                    );

  return o;
}


/* interrupts_disable(): return the same as interrupts_enable()
 * disabling interrupts.
 */
static inline ipl_t interrupts_disable(void)
{
  ipl_t o;

  __asm__ volatile (
                    "pushfq\n"
                    "popq %0\n"
                    "cli\n"
                    : "=r" (o)
                    );

  return o;
}


static inline void count_interrupt(void)
{
  if( online_cpus != 0 ) {
    inc_css_field(irq_count);
  }
}

static inline void discount_interrupt(void)
{
  if( online_cpus != 0 ) {
    dec_css_field(irq_count);
  }
}

static inline bool is_interrupts_enabled(void)
{
  return (interrupts_read() & 0x200) != 0 ? 1: 0;
}

#define interrupts_save_and_disable(state)      \
    state=is_interrupts_enabled();              \
    interrupts_disable()

#define interrupts_restore(state)               \
    if( state ) {                               \
        interrupts_enable();                    \
    }

#endif

