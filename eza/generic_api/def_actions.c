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
 * eza/generic_api/def_actions.c: implementation of prioritized, preemptible IRQ
 *                                deferred skiplist-based actions.
 */

#include <eza/arch/types.h>
#include <eza/def_actions.h>
#include <config.h>
#include <eza/smp.h>
#include <eza/spinlock.h>
#include <eza/task.h>
#include <eza/arch/bitwise.h>
#include <eza/arch/current.h>
#include <eza/event.h>
#include <eza/arch/bits.h>
#include <config.h>

static percpu_def_actions_t cpu_actions[CONFIG_NRCPUS];
static SPINLOCK_DEFINE(big_action_lock);

void initialize_deffered_actions(void)
{
  ulong_t i;

  for(i=0;i<CONFIG_NRCPUS;i++) {
    percpu_def_actions_t *a=&cpu_actions[i];
    memset(a,0,sizeof(*a));
    list_init_head(&a->pending_actions);
    spinlock_initialize(&a->lock);
  }
}

void schedule_deffered_actions(list_head_t *actions)
{
  percpu_def_actions_t *acts=&cpu_actions[cpu_id()];
  long is;
  deffered_irq_action_t *a;

  spinlock_lock_irqsave(&acts->lock,is);

  if( list_is_empty(&acts->pending_actions) ) {
    list_move2head(&acts->pending_actions,actions);
    a=container_of(list_node_first(&acts->pending_actions),
                   deffered_irq_action_t,node);

    if( a->priority <= current_task()->priority ) {
      arch_sched_set_def_works_pending();
    }
  } else {
  repeat:
    while( !list_is_empty(actions) ) {
      list_node_t *ln;
      a=container_of(list_node_first(actions),deffered_irq_action_t,node);

      list_del(&a->node);

      if( a->priority <= current_task()->priority ) {
        arch_sched_set_def_works_pending();
      }

      list_for_each(&acts->pending_actions,ln) {
        deffered_irq_action_t *da=container_of(ln,deffered_irq_action_t,node);

        if( da->priority > a->priority ) {
          list_insert_before(&a->node,ln);
          goto repeat;
        } else if( da->priority == a->priority ) {
          list_add2tail(&da->head,&a->node);

          if( !list_is_empty(&a->head) ) {
            list_move2tail(&da->head,&a->head);
          }
          goto repeat;
        }
      }
      list_add2tail(&acts->pending_actions,&a->node);
    }
  }

  spinlock_unlock_irqrestore(&acts->lock,is);
}

void schedule_deffered_action(deffered_irq_action_t *a) {
  percpu_def_actions_t *acts=&cpu_actions[cpu_id()];
  long is;

  spinlock_lock_irqsave(&acts->lock,is);
  spinlock_lock(&big_action_lock);

  if( a->__host ) {
    goto out_unlock; /* Action is already on the list. */
  }

  a->__host=acts;

  if( list_is_empty(&acts->pending_actions) ) {
    list_add2tail(&acts->pending_actions,&a->node);
  } else {
    deffered_irq_action_t *da;
    list_node_t *ln;

    list_for_each(&acts->pending_actions,ln) {
      da=container_of(ln,deffered_irq_action_t,node);

      if( da->priority > a->priority ) {
        list_insert_before(&a->node,ln);
        goto out;
      } else if( da->priority == a->priority ) {
        list_add2tail(&da->head,&a->node);

        if( !list_is_empty(&a->head) ) {
          list_move2tail(&da->head,&a->head);
        }
        goto out;
      }
    }
    list_add2tail(&acts->pending_actions,&a->node);
  }

  out:
  /* Current task needs to be rescheduled as soon as possible. */
  if( a->priority <= current_task()->priority ) {
    arch_sched_set_def_works_pending();
  }
out_unlock:
  spinlock_unlock(&big_action_lock);
  spinlock_unlock_irqrestore(&acts->lock,is);
}

void execute_deffered_action(deffered_irq_action_t *a)
{
  switch( a->type ) {
    case DEF_ACTION_EVENT:
      event_raise(&a->d._event);
      break;
    case DEF_ACTION_SIGACTION:
      kprintf_dbg("execute_deffered_action(): prio=%d. Sending signal %d to %d\n",
                  a->priority,a->d.siginfo.si_signo,a->d.siginfo.si_pid);
      send_process_siginfo(a->d.siginfo.si_pid,&a->d.siginfo,a->kern_priv);
      break;
    case  DEF_ACTION_UNBLOCK:
      activate_task(a->d.target);
      break;
  }
}

void fire_deffered_actions(void)
{
  percpu_def_actions_t *acts=&cpu_actions[cpu_id()];
  long is,fired;
  deffered_irq_action_t *action;

  /* To prevent recursive invocations. */
  spinlock_lock_irqsave(&acts->lock,is);
  if( acts->executers || list_is_empty(&acts->pending_actions) ) {
    preempt_enable();
    spinlock_unlock_irqrestore(&acts->lock,is);
    return;
  } else {
    acts->executers++;
  }
  spinlock_unlock_irqrestore(&acts->lock,is);

  fired=0;
  do {
    spinlock_lock_irqsave(&acts->lock,is);
    action=NULL;

    if( !list_is_empty(&acts->pending_actions) ) {
      action=container_of(list_node_first(&acts->pending_actions),
                          deffered_irq_action_t,node);

      if( current_task()->priority >= action->priority ) {
        list_node_t *prev=action->node.prev;

        /* Remove action under protection of the lock. */
        spinlock_lock(&big_action_lock);
        list_del(&action->node);
        action->__host=NULL;
        spinlock_unlock(&big_action_lock);

        if( !list_is_empty(&action->head) ) {
          deffered_irq_action_t *a=container_of(list_node_first(&action->head),
                                                deffered_irq_action_t,node);
          list_del(&a->node);

          if( !list_is_empty(&action->head) ) {
            list_move2head(&a->head,&action->head);
          }

          a->node.prev=prev;
          a->node.next=prev->next;
          prev->next->prev=&a->node;
          prev->next=&a->node;
        }
        arch_sched_set_def_works_pending();
      } else {
        /* Bad luck - current thread has higher priority than any of pending
         * deffered actions.
         */
        action=NULL;
      }
    }

    if( !action ) { /* No valid actions found. */
      arch_sched_reset_def_works_pending();
    }
    spinlock_unlock_irqrestore(&acts->lock,is);

    if( action ) {
      execute_deffered_action(action);
      fired++;
    }
  } while(action != NULL && fired < CONFIG_MAX_DEFERRED_ACTIONS_PER_TICK);

  spinlock_lock_irqsave(&acts->lock,is);
  acts->executers--;
  preempt_enable();   /* Trigger preemption. */
  spinlock_unlock_irqrestore(&acts->lock,is);
}
