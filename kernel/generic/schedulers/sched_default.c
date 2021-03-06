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
 * (C) Copyright 2008 Michael Tsymbalyuk <mtzaurus@gmail.com>
 *
 * mstring/generic_api/schedulers/sched_default.c: Implementation of EZA default
 *    scheduler.
 */

#include <arch/types.h>
#include <mstring/resource.h>
#include <mm/vmm.h>
#include <mm/page_alloc.h>
#include <mm/slab.h>
#include <mstring/kstack.h>
#include <sync/spinlock.h>
#include <arch/current.h>
#include <ds/list.h>
#include <mstring/scheduler.h>
#include <mstring/errno.h>
#include <mstring/sched_default.h>
#include <mstring/assert.h>
#include <mstring/string.h>
#include <arch/preempt.h>
#include <mstring/swks.h>
#include <arch/interrupt.h>
#include <mstring/interrupt.h>
#include <arch/preempt.h>
#include <arch/asm.h>
#include <mstring/kconsole.h>
#include <mstring/gc.h>
#include <arch/apic.h>
#include <arch/current.h>
#include <mstring/signal.h>
#include <mstring/def_actions.h>
#include <config.h>

static memcache_t * sched_task_data_cache; /* slab cache for task data */
/* Our own scheduler. */
static struct __scheduler mstring_default_scheduler;

/* Glabal per-CPU array. */
mstring_sched_cpudata_t *sched_cpu_data[EZA_SCHED_CPUS];
static spinlock_t cpu_data_lock;

#define LOCK_CPU_ARRAY()                        \
    spinlock_lock(&cpu_data_lock)

#define UNLOCK_CPU_ARRAY()                      \
    spinlock_unlock(&cpu_data_lock)

#define CPU_SCHED_DATA() sched_cpu_data[cpu_id()]

#define PRIO_TO_TIMESLICE(p) (p*3*1)

#define LOCK_EZA_SCHED_DATA(t)                  \
  spinlock_lock(&t->sched_lock)

#define UNLOCK_EZA_SCHED_DATA(t) \
  spinlock_unlock(&t->sched_lock)

#define __ENTER_USER_ATOMIC()                 \
    interrupts_disable()

#define __LEAVE_USER_ATOMIC()                   \
    interrupts_enable();                        \
    cond_reschedule()

#define migration_thread(cpu)  gc_threads[cpu][MIGRATION_THREAD_IDX]

static void __initialize_cpu_sched_data(mstring_sched_cpudata_t *queue, cpu_id_t cpu);

static mstring_sched_taskdata_t *__allocate_task_sched_data(void)
{
  return alloc_from_memcache(sched_task_data_cache, 0);
}

int sched_verbose=0;

static mstring_sched_cpudata_t *__allocate_cpu_sched_data(cpu_id_t cpu) {
  int pages;
  pages = sizeof(mstring_sched_cpudata_t) / PAGE_SIZE;
  if (sizeof(mstring_sched_cpudata_t) % PAGE_SIZE)
    pages++;
  page_frame_t *page = alloc_pages(pages, MMPOOL_KERN | AF_CONTIG);
  mstring_sched_cpudata_t *cpudata = (mstring_sched_cpudata_t *)pframe_to_virt(page);
  if( cpudata != NULL ) {
    __initialize_cpu_sched_data(cpudata, cpu);
  }

  return cpudata;
}

/* This function is just stub for now.
  It is called only if add_cpu failed after allocating pages
  for mstring_sched_cpudata_t. If cpu_data allocation will
  be implemeted via slab, this function should perform slab freing. */
static void __free_cpu_sched_data(mstring_sched_cpudata_t *data)
{
  /* TODO: [mt] Free allocated pages! */
}

static void __free_task_sched_data(mstring_sched_taskdata_t *data)
{
  memfree(data);
}

static void __initialize_cpu_sched_data(mstring_sched_cpudata_t *cpudata, cpu_id_t cpu)
{
  uint32_t arr,i;

  for( arr=0; arr<EZA_SCHED_NUM_ARRAYS; arr++ ) {
    mstring_sched_prio_array_t *array = &cpudata->arrays[arr];
    list_head_t *lh = &array->queues[0];

    array->num_tasks=0;
    memset(&array->bitmap[0], EZA_SCHED_BITMAP_PATTERN,
           sizeof(mstring_sched_type_t)*EZA_SCHED_TOTAL_WIDTH);

    for( i=0; i<EZA_SCHED_TOTAL_PRIOS; i++ ) {
      list_init_head(lh);
      lh++;
    }
  }

  spinlock_initialize(&cpudata->lock, "CPU data");
  bound_spinlock_initialize(&cpudata->__lock,cpu);
  cpudata->active_array = &cpudata->arrays[0];
  cpudata->expired_array = &cpudata->arrays[1];

  /* Initialize scheduler statistics for this CPU. */
  cpudata->stats = &swks.cpu_stat[cpu].sched_stats;
  cpudata->cpu_id = cpu;
}

static int __setup_new_task(task_t *task)
{
  mstring_sched_taskdata_t *sdata = __allocate_task_sched_data();

  if( sdata == NULL ) {
    return -ENOMEM;
  }

  list_init_node(&sdata->runlist);
  spinlock_initialize(&sdata->sched_lock, "Scheduler data");

  LOCK_TASK_STRUCT(task);
  task->sched_data = sdata;
  task->scheduler = &mstring_default_scheduler;
  sdata->task = task;
  UNLOCK_TASK_STRUCT(task);
  return 0;
}

/* NOTE: This function relies on _current_ state of the task being processed.
 */
static inline void __recalculate_timeslice_and_priority(task_t *task)
{
  mstring_sched_taskdata_t *tdata = EZA_TASK_SCHED_DATA(task);

  /* Recalculate priority. */
  task->priority = task->static_priority;

  switch(task->state) {
    case TASK_STATE_SLEEPING:
      break;
    default:
      break;
  }

  /* Recalculate timeslice. */
  tdata->time_slice = PRIO_TO_TIMESLICE(task->priority);

  /* Are we jailed ? */
  if( tdata->max_timeslice != 0 ) {
    if( tdata->time_slice > tdata->max_timeslice ) {
      tdata->time_slice = tdata->max_timeslice;
    }
  }
}

int sched_verbose1;

static void def_init(void)
{
  sched_task_data_cache = create_memcache("Def sched task data",
                                        sizeof(mstring_sched_taskdata_t),1,
                                       MMPOOL_KERN | SMCF_IMMORTAL |
                                       SMCF_LAZY | SMCF_UNIQUE);
  if( !sched_task_data_cache ) {
    panic( "Default scheduler init(): Can't create task data memcache !" );
  }
/*
  sched_cpu_data_cache = create_memcache("Def sched cpu data",
                                        sizeof(mstring_sched_cpudata_t),1,
                                       MMPOOL_KERN | SMCF_IMMORTAL |
                                       SMCF_LAZY | SMCF_UNIQUE);
  if( !sched_cpu_data_cache ) {
    panic( "Default scheduler init(): Can't create cpu data memcache !" );
  }*/
}

static cpu_id_t def_cpus_supported(void){
  return EZA_SCHED_CPUS;
}

static int def_add_cpu(cpu_id_t cpu)
{
  mstring_sched_cpudata_t *cpudata;
  int r;

  if(cpu >= EZA_SCHED_CPUS || sched_cpu_data[cpu] != NULL) {
    return -EINVAL;
  }

  cpudata = __allocate_cpu_sched_data(cpu);
  if( cpudata == NULL ) {
    panic( "Can't allocate data for CPU N%d\n",cpu );
    return -ENOMEM;
  }

  LOCK_CPU_ARRAY();

  if( sched_cpu_data[cpu] == NULL ) {
    sched_cpu_data[cpu] = cpudata;
    cpudata->running_task=idle_tasks[cpu];
    r = 0;
  } else {
    r = -EEXIST;
  }

  UNLOCK_CPU_ARRAY();

  if( r != 0 ) {
    __free_cpu_sched_data( cpudata );
  }

  return r;
}

static void def_scheduler_tick(int op)
{
  task_t *current = current_task();
  mstring_sched_cpudata_t *cpudata = CPU_SCHED_DATA();
  mstring_sched_taskdata_t *tdata;
  sched_discipline_t discipl;

  if(cpudata == NULL) {
    return;
  }

  /* Idle task ?  */
  if( !current->pid ) {
    update_idle_tick_statistics(cpudata->stats);
    return;
  }

  tdata = EZA_TASK_SCHED_DATA(current);
  if( !tdata ) {
    return;
  }

  discipl = tdata->sched_discipline;

  LOCK_CPU_SCHED_DATA(cpudata);

  switch( op ) {
    case __SCHED_TICK_LAST:
      tdata->time_slice=0;
      break;
    default: /* __SCHED_TICK_NORMAL */
      if( discipl != SCHED_FIFO ) {
        tdata->time_slice--;
      }
      break;
  }

  if( !tdata->time_slice ) {
    if( task_on_runlist(tdata) ) {
      if( discipl == SCHED_RR || discipl == SCHED_FIFO ) {
        __remove_task_from_array(cpudata->active_array,current);
        __recalculate_timeslice_and_priority(current);
        __add_task_to_array(cpudata->active_array,current);
      } else {
        __remove_task_from_array(cpudata->active_array,current);
        __recalculate_timeslice_and_priority(current);
        __add_task_to_array(cpudata->expired_array,current);
      }
    }
    sched_set_current_need_resched();
  }

  UNLOCK_CPU_SCHED_DATA(cpudata);
}

static int def_add_task(task_t *task)
{
  cpu_id_t cpu = cpu_id();
  mstring_sched_taskdata_t *sdata;

  if( sched_cpu_data[cpu] == NULL || task->state != TASK_STATE_JUST_BORN ) {
    return -EINVAL;
  }

  if( task->sched_data != NULL ) {
    kprintf( KO_WARNING "def_add_task(): Task being added already attached to a scheduler !\n" );
    return -EBUSY;
  }

  if( __setup_new_task(task) != 0 ) {
    return -ENOMEM;
  }

  LOCK_TASK_STRUCT(task);

  sdata = (mstring_sched_taskdata_t *)task->sched_data;
  task->static_priority = EZA_SCHED_INITIAL_TASK_PRIORITY;
  task->priority = EZA_SCHED_INITIAL_TASK_PRIORITY;
  task->cpu = cpu;
  sdata->sched_discipline = SCHED_OTHER; /* TODO: [mt] must be SCHED_ADAPTIVE */
  sdata->max_timeslice = 0;
  sdata->array = NULL;

  UNLOCK_TASK_STRUCT(task);
  return 0;
}

#ifdef CONFIG_TRACE_CURRENT
static pid_t __current_cpu_task_pid[CONFIG_NRCPUS];
static tid_t __current_cpu_task_tid[CONFIG_NRCPUS];
#endif

static void def_schedule(void)
{
  mstring_sched_cpudata_t *sched_data = CPU_SCHED_DATA();
  task_t *current = current_task();
  task_t *next;
  bool need_switch,ints_enabled,arrays_switched=false;

  /* From this moment we are in atomic context until 'arch_activate_task()'
   * finishes its job or until interrupts will be enabled in no context
   * switch is required.
   */
  ints_enabled=is_interrupts_enabled();
  if( is_interrupts_enabled() ) {
    interrupts_disable();
  }

  __LOCK_CPU_SCHED_DATA(sched_data);
  sched_reset_current_need_resched();

get_next_task:
  next = __get_most_prioritized_task(sched_data);
  if( next == NULL ) {
    if( !arrays_switched ) {
      mstring_sched_prio_array_t *a=sched_data->active_array;
      sched_data->active_array=sched_data->expired_array;
      sched_data->expired_array=a;

      arrays_switched=true;
      goto get_next_task;
    } else {
      /* No luck - schedule idle task. */
      next = idle_tasks[sched_data->cpu_id];
      sched_data->stats->idle_switches++;
    }
  }
  sched_data->stats->task_switches++;

  if(current->state == TASK_STATE_RUNNING) {
    current->state = TASK_STATE_RUNNABLE;
  }

  /* Do we really need to swicth hardware context ? */
  if( next != current ) {
    sched_data->running_task=next;
    need_switch = true;
  } else {
    need_switch = false;
  }

  if( next->state == TASK_STATE_RUNNABLE ) {
    next->state = TASK_STATE_RUNNING;
  }

#ifdef CONFIG_TRACE_CURRENT
  __current_cpu_task_pid[cpu_id()]=next->pid;
  __current_cpu_task_tid[cpu_id()]=next->tid;
#endif

  __UNLOCK_CPU_SCHED_DATA(sched_data);

  if( need_switch ) {
      arch_activate_task(next);
      update_deferred_actions();
  }

  if( ints_enabled ) {
    interrupts_enable();
  }
}

/* NOTE: Currently works only for current task ! */
static int def_del_task(task_t *task)
{
  int r;
  gc_action_t action;
  mstring_sched_cpudata_t *sched_data=CPU_SCHED_DATA();

  if( task != current_task() ) {
    return -EINVAL;
  }

  /* Prepare a deferred action. */
  gc_init_action(&action,cleanup_thread_data,task);
  action.type=GC_TASK_RESOURCE;
  action.dtor=NULL;

  interrupts_disable();
  LOCK_TASK_STRUCT(task);

  if( task->scheduler != &mstring_default_scheduler || !task->sched_data ) {
    r=-EINVAL;
    UNLOCK_TASK_STRUCT(task);
    interrupts_enable();
  } else {
    mstring_sched_taskdata_t *sdata=EZA_TASK_SCHED_DATA(task);
     LOCK_CPU_SCHED_DATA(sched_data);
    __remove_task_from_array(sdata->array,task);
    sched_data->stats->active_tasks--;
    UNLOCK_CPU_SCHED_DATA(sched_data);
    __free_task_sched_data(sdata);
    gc_schedule_action(&action);
    sched_set_current_need_resched();

    UNLOCK_TASK_STRUCT(task);
    interrupts_enable();

    /* Leave the CPU forever. */
   def_schedule();
  }

  return r;
}

static void def_reset(void)
{
  int i;

  spinlock_initialize(&cpu_data_lock, "CPU data");

  for(i=0;i<EZA_SCHED_CPUS;i++) {
    sched_cpu_data[i] = NULL;
  }
}

static inline void __reschedule_task(task_t *t)
{
  set_task_need_resched(t);
  if( t->cpu != cpu_id() ) {
      //apic_broadcast_ipi_vector(SCHEDULER_IPI_IRQ_VEC);
  }
}

int __big_verbose=0;

static int __change_task_state(task_t *task,task_state_t new_state,
                               deferred_sched_handler_t h,void *data,
                               ulong_t mask)
{
  long is;
  int r=0;
  mstring_sched_cpudata_t *sched_data;
  task_state_t prev_state;
  mstring_sched_taskdata_t *tdata = EZA_TASK_SCHED_DATA(task);

  if( task->cpu != cpu_id() ) {
    h=NULL;
  }

  sched_data=get_task_sched_data_locked(task,&is,true);
  if( h != NULL && !h(data) ) {
    r=-EAGAIN;
    goto out_unlock;
  }

  prev_state=task->state;

  if( (prev_state != new_state) && (prev_state & mask) ) {
    r=-EINVAL;

    switch(new_state) {
      case TASK_STATE_RUNNABLE:
        r=0;

        if( prev_state == TASK_STATE_RUNNING ) {
          break;
        }

        __recalculate_timeslice_and_priority(task);
        task->state = TASK_STATE_RUNNABLE;
        __add_task_to_array(sched_data->active_array,task);
        sched_data->stats->active_tasks++;

        if( task->priority < sched_data->running_task->priority ) {
          __reschedule_task(task);
        }
        break;
      case TASK_STATE_SLEEPING:
        if( task_was_interrupted(task) ) {
          r=-EINTR;
          break;
        }

        /* Fallthrough. */
      case TASK_STATE_STOPPED:
      case TASK_STATE_SUSPENDED:
        if( task->state == TASK_STATE_RUNNABLE
            || task->state == TASK_STATE_RUNNING
            || task->state == TASK_STATE_ZOMBIE ) {

          __remove_task_from_array(tdata->array,task);
          sched_data->stats->active_tasks--;
          sched_data->stats->sleeping_tasks++;
          task->state=new_state;

          if( task == sched_data->running_task ) {
            __reschedule_task(task);
          }
          r=0;
        } else if( task->state == TASK_STATE_SLEEPING ) {
          task->state=new_state;
          r=0;
        }
        break;
      default:
        break;
    }
  } else {
    r=0;
  }

out_unlock:
  __UNLOCK_CPU_SCHED_DATA(sched_data);
  interrupts_restore(is);
  cond_reschedule();
  return r;
}

int def_change_task_state(task_t *task,task_state_t new_state,ulong_t mask)
{
  return __change_task_state(task,new_state,NULL,NULL,mask);
}

static int def_setup_idle_task(task_t *task)
{
  mstring_sched_taskdata_t *sdata;

  if( __setup_new_task(task) != 0 ) {
    return -ENOMEM;
  }

  sdata = (mstring_sched_taskdata_t *)task->sched_data;
  task->priority = task->static_priority = EZA_SCHED_IDLE_TASK_PRIO;
  sdata->time_slice = 0;

  LOCK_TASK_STRUCT(task);
  task->state = TASK_STATE_RUNNING;
  task->scheduler = &mstring_default_scheduler;
  task->sched_data = sdata;
  UNLOCK_TASK_STRUCT(task);

  return 0;
}

/* NOTE: Scheduler data must be locked upon entering this function ! */
static void __shuffle_task(task_t *target,mstring_sched_cpudata_t *sched_data, uint32_t new_prio)
{
  task_t *mpt;

  /* In case task is sleeping we must be able to change its priority quitely. */
  if (target->state != TASK_STATE_RUNNING && target->state != TASK_STATE_RUNNABLE) {
    target->priority = target->static_priority = new_prio;
    return;
  }

  __remove_task_from_array(sched_data->active_array,target);
  target->priority = target->static_priority = new_prio;
  __add_task_to_array(sched_data->active_array,target);

  mpt=__get_most_prioritized_task(sched_data);
  if( mpt != NULL ) {
    if( mpt->priority < sched_data->running_task->priority ) {
      __reschedule_task(target);
    }
  } else {
    panic( "__shuffle_task(): No runnable tasks after adding a task !" );
  }
}

/* NOTE: Upon entering this routine target task is unlocked.
 */
static int def_scheduler_control(task_t *target,ulong_t cmd,ulong_t arg)
{
  long is;
  int r=-EINVAL;
  mstring_sched_taskdata_t *sdata = EZA_TASK_SCHED_DATA(target);
  mstring_sched_cpudata_t *sched_data=get_task_sched_data_locked(target,&is,true);

  switch(cmd) {
    /* Getters. */
    case SYS_SCHED_CTL_GET_MAX_TIMISLICE:
      r=sdata->max_timeslice;
      break;
    case SYS_SCHED_CTL_GET_POLICY:
      r=sdata->sched_discipline;
      break;
    /* Setters. */
    case SYS_SCHED_CTL_SET_POLICY:
      if( arg == SCHED_RR || arg == SCHED_FIFO || arg == SCHED_OTHER ) {
        sdata->sched_discipline = arg;
        r=0;
      }
      break;
    case SYS_SCHED_CTL_SET_PRIORITY:
      if( arg <= EZA_SCHED_PRIORITY_MAX ) {
        if( !(target->flags & TF_USPC_BLOCKED) ) {
          __shuffle_task(target,sched_data,arg);
          r=0;
        } else {
          r=-EBUSY;
        }
      }
      break;
    case SYS_SCHED_CTL_SET_MAX_TIMISLICE:
      if( arg > 0 && arg < HZ ) {
        sdata->max_timeslice=arg;
        r=0;
      }
      break;
    case SYS_SCHED_CTL_SET_STATE:
      __UNLOCK_CPU_SCHED_DATA_INT(sched_data,is);
      return def_change_task_state(target,arg,__ALL_TASK_STATE_MASK);
  }
  __UNLOCK_CPU_SCHED_DATA_INT(sched_data,is);
  return r;
}

static int def_change_task_state_deferred(task_t *task, task_state_t state,
                                              deferred_sched_handler_t handler,
                                               void *data,ulong_t mask)
{
  return  __change_task_state(task,state,handler,data,mask);
}

static struct __scheduler mstring_default_scheduler = {
  .id = "Eza default scheduler",
  .init = def_init,
  .cpus_supported = def_cpus_supported,
  .add_cpu = def_add_cpu,
  .scheduler_tick = def_scheduler_tick,
  .add_task = def_add_task,
  .del_task = def_del_task,
  .schedule = def_schedule,
  .reset = def_reset,
  .change_task_state = def_change_task_state,
  .change_task_state_deferred = def_change_task_state_deferred,
  .setup_idle_task = def_setup_idle_task,
  .scheduler_control = def_scheduler_control,
};

scheduler_t *get_default_scheduler(void)
{
  return &mstring_default_scheduler;
}

void sys_sched_yield(void)
{
  interrupts_disable();
  def_scheduler_tick(__SCHED_TICK_LAST);
  def_schedule();
  interrupts_enable();
  return;
}
