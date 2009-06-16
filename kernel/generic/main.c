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
 * (c) Copyright 2005,2008 Tirra <tirra.newly@gmail.com>
 *
 * mstring/generic_api/main.c: main routine, this functions called after bootstrap
 *                          initialization made
 *
 */

#include <config.h>
#include <mm/vmm.h>
#include <mm/slab.h>
#include <mstring/kprintf.h>
#include <server.h>
#include <mstring/smp.h>
#include <arch/task.h>
#include <mstring/interrupt.h>
#include <mstring/scheduler.h>
#include <arch/platform.h>
#include <mstring/swks.h>
#include <arch/scheduler.h>
#include <arch/smp.h>
#include <ipc/ipc.h>
#include <mstring/resource.h>
#include <arch/interrupt.h>
#include <mstring/gc.h>
#include <mstring/signal.h>

init_t init={ /* initially created for userspace task, requered for servers loading */
   .c=0
};

/* current context safe */
context_t crsc;

extern void initialize_common_hardware(void);
extern void initialize_timer(void);

static void main_routine_stage1(void)
{
  /* Initialize PICs and setup common interrupt handlers. */
  set_cpu_online(0,1);  /* We're online. */
  sched_add_cpu(0);
  initialize_ipc();
  initialize_signals();
  initialize_gc();

  arch_initialize_irqs();
  arch_specific_init();

  /* Initialize known hardware devices. */
  initialize_common_hardware();
  initialize_resources();
  /* Since the PIC is initialized, all interrupts from the hardware
   * is disabled. So we can enable local interrupts since we will
   * receive interrups from the other CPUs via LAPIC upon unleashing
   * the other CPUs.
   */
  setup_time();
  interrupts_enable();
  initialize_swks();

  /* OK, we can proceed. */
  spawn_percpu_threads();
  server_run_tasks();

  /* Enter idle loop. */
  kprintf( "CPU #0 is entering idle loop. Current task: %p, CPU ID: %d\n",
           current_task(), cpu_id() );
  idle_loop();
}

void kernel_main(void)
{
  arch_init();
  initialize_irqs();

  kprintf("[MB] Modules: %d\n",init.c);
  mm_initialize();
  slab_allocator_init();
  vmm_initialize();
  initialize_scheduler();

  initialize_timer();
  
  /* Now we can switch stack to our new kernel stack, setup any arch-specific
   * contexts, etc.
   */
  arch_activate_idle_task(0);
  /* Now we can continue initialization with properly initialized kernel
   * stack frame.
   */
  
  main_routine_stage1();
}

#ifdef CONFIG_SMP
static void main_smpap_routine_stage1(cpu_id_t cpu)
{
  arch_ap_specific_init();

  /* We're online. */
  set_cpu_online(cpu,1);
  sched_add_cpu(cpu);

  interrupts_enable();

  spawn_percpu_threads();

  /* Entering idle loop. */
  kprintf( "CPU #%d is entering idle loop. Current task: %p, CPU: %d, ATOM: %d\n",
           cpu, current_task(), cpu_id(), in_atomic() );

  idle_loop();
}

void main_smpap_routine(void)
{
  static cpu_id_t cpu=1;

  kprintf("CPU#%d Hello folks! I'm here\n", cpu);

  /* Perform generic CPU initialization. Memory will be initialized later. */
  arch_cpu_init(cpu);

  /* Now we can switch stack to our new kernel stack, setup any arch-specific
   * contexts, etc.
   */
  arch_activate_idle_task(cpu);
  cpu++;

  /* Continue CPU initialization in new context. */
  main_smpap_routine_stage1(cpu - 1);
}
#endif
