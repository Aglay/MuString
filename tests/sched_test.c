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
 * test/sched_test.c: tests for Muistring scheduler subsystem.
 */

#include <config.h>
#include <mstring/kernel.h>
#include <mstring/kprintf.h>
#include <mstring/smp.h>
#include <arch/scheduler.h>
#include <arch/types.h>
#include <mstring/task.h>
#include <mstring/scheduler.h>
#include <mstring/swks.h>
#include <mstring/string.h>
#include <arch/preempt.h>
#include <sync/spinlock.h>
#include <ipc/ipc.h>
#include <ipc/port.h>
#include <arch/asm.h>
#include <arch/preempt.h>
#include <kernel/syscalls.h>
#include <mstring/uinterrupt.h>
#include <ipc/poll.h>
#include <mstring/gc.h>
#include <ipc/gen_port.h>
#include <ipc/channel.h>
#include <test.h>
#include <mm/slab.h>
#include <mstring/errno.h>
#include <sync/spinlock.h>
#include <arch/spinlock.h>

#define TEST_ID "Scheduler tests"
#define SERVER_ID "[Migration test] "
#define TRAVELLER_ID "[CPU Traveller] "

#define TRAVELLER_SLEEP_TICKS  300

typedef struct __sched_test_ctx {
  test_framework_t *tf;
  ulong_t server_pid;
  bool tests_finished;
} sched_test_ctx_t;

typedef struct __sched_thread_data {
  ulong_t target_cpu;
  test_framework_t *tf;
  task_t *task;
} sched_thread_data_t;

static void __traveller_thread(void *d)
{
  sched_thread_data_t *td=(sched_thread_data_t*)d;
  test_framework_t *tf=td->tf;
  uint64_t target_tick=swks.system_ticks_64 + TRAVELLER_SLEEP_TICKS;
  int r;
  ulong_t back_cpu=0;

  tf->printf(TRAVELLER_ID "PID: %d, Starting on CPU %d, Target CPU: %d\n",
             current_task()->pid,cpu_id(),td->target_cpu);

  if( cpu_id() != td->target_cpu ) {
    tf->failed();
  } else {
    tf->passed();
  }

  /* Simulate some activity ... */
  tf->printf(TRAVELLER_ID "Entering long busy-wait loop.\n");
  while(swks.system_ticks_64 < target_tick) {
  }
  tf->printf(TRAVELLER_ID "Leaving long busy-wait loop.\n");

  tf->printf(TRAVELLER_ID "Moving back to CPU #%d\n",back_cpu );
  r=sys_scheduler_control(current_task()->pid,SYS_SCHED_CTL_SET_CPU,
                          back_cpu);
  if( r ) {
    tf->printf(TRAVELLER_ID "Can't move back to CPU #%d: r=%d\n",
               back_cpu,r);
    tf->failed();
  } else {
    r=sys_scheduler_control(current_task()->pid,SYS_SCHED_CTL_GET_CPU,
                            0);
    if( r == back_cpu ) {
      tf->passed();
    } else {
      tf->printf(TRAVELLER_ID "CPU ID differs after migration ! %d:%d\n",
                 r,back_cpu);
      tf->failed();
    }
  }

  sys_exit(0);
}

static void __migration_test(void *d)
{
  sched_test_ctx_t *tctx=(sched_test_ctx_t*)d;
  test_framework_t *tf=tctx->tf;
  sched_thread_data_t *thread_data[CONFIG_NRCPUS-1];
  int i,r;

  if( do_scheduler_control(current_task(),SYS_SCHED_CTL_SET_POLICY,
                           SCHED_FIFO) ) {
    tf->printf(SERVER_ID "Can't change my scheduling policy to SCHED_FIFO !\n" );
    tf->abort();
  }

  for(i=0;i<CONFIG_NRCPUS-1;i++) {
    sched_thread_data_t *td=memalloc(sizeof(*td));
    task_t *t;

    if( !td ) {
      tf->printf(SERVER_ID"Can't allocate data buffer !\n" );
      tf->abort();
    }

    td->target_cpu=i+1;
    td->tf=tf;
    thread_data[i]=td;

    if( kernel_thread(__traveller_thread,td,&t ) ) {
      tf->printf( SERVER_ID "Can't create traveller thread !\n" );
      tf->abort();
    }

    td->task=t;

    tf->printf(SERVER_ID"Moving %d to CPU #%d\n",
               t->pid,td->target_cpu);
    r=sys_scheduler_control(t->pid,SYS_SCHED_CTL_SET_CPU,td->target_cpu);
    if( r ) {
      tf->printf(SERVER_ID "Can't move task %d to CPU %d: r=%d\n",
                 t->pid,td->target_cpu,r);
      tf->failed();
    }
    r=sys_scheduler_control(t->pid,SYS_SCHED_CTL_GET_CPU,0);
    if( r != td->target_cpu ) {
      tf->printf(SERVER_ID "CPU id mismatch after moving task ! %d:%d\n",
                 r,td->target_cpu);
    }
  }

  tf->printf(SERVER_ID "All threads we processed.\n");
  sleep(HZ);
  return;

  /* Now change state for all remote tasks. */
  tf->printf(SERVER_ID "Now put all remote tasks into sleep.\n");
  for( i=0;i<CONFIG_NRCPUS-1;i++ ) {
    tf->printf(SERVER_ID "Putting into sleep task %d (CPU: %d)\n",
               thread_data[i]->task->pid,
               thread_data[i]->target_cpu);
    r=sched_change_task_state(thread_data[i]->task,TASK_STATE_STOPPED);
    if( r ) {
      tf->printf(SERVER_ID "Can't change state of PID %d (CPU %d): r=%d\n",
                 thread_data[i]->task->pid,
                 thread_data[i]->target_cpu,r);
      tf->failed();
    } else {
      tf->passed();
    }
  }

  sleep(HZ*100);

  /* Now restore state for all remote tasks. */
  tf->printf(SERVER_ID "Now wake up all remote tasks.\n");
  for( i=0;i<CONFIG_NRCPUS-1;i++ ) {
    tf->printf(SERVER_ID "Waking up task %d (CPU: %d)\n",
               thread_data[i]->task->pid,
               thread_data[i]->target_cpu);
    r=sched_change_task_state(thread_data[i]->task,TASK_STATE_RUNNABLE);
    if( r ) {
      tf->printf(SERVER_ID "Can't change state of PID %d (CPU %d): r=%d\n",
                 thread_data[i]->task->pid,
                 thread_data[i]->target_cpu,r);
      tf->failed();
    } else {
      tf->passed();
    }
  }

  /* Resource cleanup */
  for(i=0;i<CONFIG_NRCPUS-1;i++) {
    memfree(thread_data[i]);
  }
}

static void __priority_test(void *d)
{
}

static void __test_thread(void *d)
{
  sched_test_ctx_t *tctx=(sched_test_ctx_t*)d;

  __migration_test(tctx);
  tctx->tf->printf(SERVER_ID "Calling priority tests.\n");
  __priority_test(tctx);

  tctx->tf->printf(SERVER_ID "All scheduler tests finished.\n");
  tctx->tests_finished=true;

  sys_exit(0);
}

static bool __sched_tests_initialize(void **ctx)
{
  sched_test_ctx_t *tctx=memalloc(sizeof(*tctx));

  if( tctx ) {
    memset(tctx,0,sizeof(*tctx));
    return true;
  }

  return false;
}

void __sched_tests_run(test_framework_t *f,void *ctx)
{
  sched_test_ctx_t *tctx=(sched_test_ctx_t*)ctx;
  tctx->tf=f;

  if( kernel_thread(__test_thread,tctx,NULL) ) {
    f->printf( "Can't create main test thread !" );
    f->abort();
  } else {
    tctx->tests_finished=false;
    f->test_completion_loop(TEST_ID,&tctx->tests_finished);
  }
}

void __sched_tests_deinitialize(void *ctx)
{
  memfree(ctx);
}

testcase_t sched_testcase={
  .id=TEST_ID,
  .initialize=__sched_tests_initialize,
  .deinitialize=__sched_tests_deinitialize,
  .run=__sched_tests_run,
};

