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
 *
 * include/eza/scheduler.h: contains main types and prototypes for the generic
 *                          scheduler layer.
 *
 */

#ifndef __SCHEDULER_H__
#define __SCHEDULER_H__ 

#include <eza/arch/types.h>
#include <eza/resource.h>
#include <mm/pt.h>
#include <eza/kstack.h>
#include <eza/spinlock.h>
#include <eza/arch/current.h>
#include <ds/list.h>
#include <eza/arch/preempt.h>

/* Macros for locking task structure. */
#define LOCK_TASK_STRUCT(t) spinlock_lock(&t->lock)
#define UNLOCK_TASK_STRUCT(t) spinlock_unlock(&t->lock)

typedef uint32_t time_slice_t;

typedef enum __task_state {
  TASK_STATE_JUST_BORN = 0,
  TASK_STATE_RUNNABLE = 1,
  TASK_STATE_RUNNING = 2,
  TASK_STATE_SLEEPING = 3,
  TASK_STATE_STOPPED = 4,
  TASK_STATE_ZOMBIE = 5,
} task_state_t;

typedef uint32_t cpu_array_t;

#define CPU_AFFINITY_ALL_CPUS 0

struct __scheduler;

/* Abstract object for scheduling. */
typedef struct __task_struct {
  pid_t pid, ppid;
  cpu_id_t cpu;

  task_state_t state;
  cpu_array_t cpu_affinity;

  kernel_stack_t kernel_stack;
  page_directory_t page_dir;
  list_node_t pid_list;

  spinlock_t lock;

  /* Scheduler-related stuff. */
  struct __scheduler *scheduler;
  void *sched_data;

  /* Arch-dependent context is located here */
  uint8_t arch_context[];
} task_t;

/* Abstract scheduler. */
typedef struct __scheduler {
  const char *id;
  list_node_t l;
  cpu_id_t (*cpus_supported)(void);
  status_t (*add_cpu)(cpu_id_t cpu);
  void (*scheduler_tick)(void);
  status_t (*add_task)(task_t *task);
  void (*schedule)(void);
  void (*reset)(void);
  status_t (*change_task_state)(task_t *task,task_state_t state);
  status_t (*setup_idle_task)(task_t *task);
} scheduler_t;


void initialize_scheduler(void);

bool sched_register_scheduler(scheduler_t *sched);
bool sched_unregister_scheduler(scheduler_t *sched);
scheduler_t *sched_get_scheduler(const char *name);

void sched_timer_tick(void);

void idle_loop(void);

extern task_t *idle_tasks[];

status_t sched_change_task_state(task_t *task,task_state_t state);
void sched_reschedule_task(task_t *task);
status_t sched_add_task(task_t *task);
status_t sched_setup_idle_task(task_t *task);
status_t sched_add_cpu(cpu_id_t cpu);

extern scheduler_t *get_default_scheduler(void);

void schedule(void);

/* Macros that deal with resceduling needs. */
extern void arch_sched_set_current_need_resched(void);
extern void arch_sched_reset_current_need_resched(void);

#define sched_set_current_need_resched() arch_sched_set_current_need_resched()
#define sched_reset_current_need_resched() arch_sched_reset_current_need_resched()

#endif

