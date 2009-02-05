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
 * (c) Copyright 2008 MadTirra <tirra.newly@gmail.com>
 * (c) Copyright 2008 Michael Tsymbalyuk <mtzaurus@gmail.com>
 *
 * eza/generic_api/timer.c: contains routines for dealing with hardware
 *                          timers. 
 *
 * - added support of software timers (Michael Tsymbalyuk);
 */

#include <ds/list.h>
#include <eza/interrupt.h>
#include <eza/errno.h>
#include <eza/spinlock.h>
#include <mlibc/string.h>
#include <eza/swks.h>
#include <mlibc/kprintf.h>
#include <eza/timer.h>
#include <eza/arch/interrupt.h>
#include <eza/time.h>
#include <eza/def_actions.h>
#include <mlibc/rbtree.h>
#include <mlibc/skiplist.h>

/*spinlock*/
static SPINLOCK_DEFINE(timer_lock);
static SPINLOCK_DEFINE(sw_timers_lock);
static SPINLOCK_DEFINE(sw_timers_list_lock);

/*list of the timers*/
static LIST_DEFINE(known_hw_timers);

static struct rb_root timers_rb_root;

#define GRAB_HW_TIMER_LOCK() spinlock_lock(&timer_lock)
#define RELEASE_HW_TIMER_LOCK() spinlock_unlock(&timer_lock)

#define LOCK_SW_TIMERS(l)  spinlock_lock_irqsave(&sw_timers_lock,l)
#define UNLOCK_SW_TIMERS(l) spinlock_unlock_irqrestore(&sw_timers_lock,l)

#define LOCK_SW_TIMERS_R(l)  spinlock_lock_irqsave(&sw_timers_lock,l)
#define UNLOCK_SW_TIMERS_R(l) spinlock_unlock_irqrestore(&sw_timers_lock,l)

#define get_major_tick(t) atomic_inc(&(t)->use_counter)
#define put_major_tick(t) if( atomic_dec_and_test(&(t)->use_counter) ) { memfree(t); }

static void init_hw_timers (void)
{
  list_init_head(&known_hw_timers);
}

void hw_timer_register (hw_timer_t *ctrl)
{
  GRAB_HW_TIMER_LOCK();
  list_add(list_node_first(&known_hw_timers), &ctrl->l);
  RELEASE_HW_TIMER_LOCK();
}

void hw_timer_generic_suspend(void)
{
}

void init_timers(void)
{
  init_hw_timers();
  initialize_deffered_actions();
}

void process_timers(void)
{
  major_timer_tick_t *mt,*major_tick=NULL;
  long is;
  struct rb_node *n;
  ulong_t mtickv=system_ticks-(system_ticks % CONFIG_TIMER_GRANULARITY);

  LOCK_SW_TIMERS_R(is);
  n=timers_rb_root.rb_node;

  while( n ) {
    mt=rb_entry(n,major_timer_tick_t,rbnode);

    if( mtickv < mt->time_x ) {
      n=n->rb_left;
    } else if( mtickv > mt->time_x )  {
      n=n->rb_right;
    } else {
      major_tick=mt;
      break;
    }
  }

  UNLOCK_SW_TIMERS_R(is);

  if( major_tick ) { /* Let's see if we have any timers for this major tick. */
    list_head_t *lh=&major_tick->minor_ticks[(system_ticks-mtickv)/MINOR_TICK_GROUP_SIZE];
    list_node_t *ln;
    timer_tick_t *tt;

    LOCK_MAJOR_TIMER_TICK(major_tick,is);
    list_for_each(lh,ln) {
      tt=container_of(ln,timer_tick_t,node);
      if( tt->time_x == system_ticks ) { /* Got something for this tick. */
        ASSERT(tt->major_tick == major_tick);

        list_del(&tt->node);
        goto out;
      }
    }
    tt=NULL;
  out:
    UNLOCK_MAJOR_TIMER_TICK(major_tick,is);

    if( tt ) { /* Let's handle all the timers we have found. */
      kprintf_dbg("process_timers(): Scheduling timers for %d.\n",tt->time_x);
      schedule_deffered_actions(&tt->actions);
    }
  }
}

void timer_cleanup_expired_ticks(void)
{
}

void delete_timer(ktimer_t *timer)
{
  long is;
  timer_tick_t *tt=&timer->minor_tick;

  if( !tt->major_tick ) { /* Ignore clear timers. */
    return;
  }

  LOCK_MAJOR_TIMER_TICK(tt->major_tick,is);
  if( list_node_is_bound(&tt->node) ) {
    /* Active root tick node, reorganize all timers associated with it. */
    if( tt->actions.head.next == &timer->da.node &&
        tt->actions.head.prev == &timer->da.node ) {
      /* The simpliest case - only one timer in this tick, no rebalance. */
      list_del(&tt->node);
      kprintf("delete_timer() [1]\n");
    } else {
      /* Need some rebalance. */
      kprintf("delete_timer() [2]\n");
      for(;;);
    }
    UNLOCK_MAJOR_TIMER_TICK(tt->major_tick,is);
  } else {
    /* In case of child timer, just remove it's deferred action from the list.*/
    spinlock_lock(&sw_timers_list_lock);
    if( list_node_is_bound(&timer->da.node) ) {
      kprintf("delete_timer() [3]\n");
      skiplist_del(&timer->da,deffered_irq_action_t,head,node);
    }
    spinlock_unlock(&sw_timers_list_lock);
    UNLOCK_MAJOR_TIMER_TICK(timer->minor_tick.major_tick,is);
  }
}

long add_timer(ktimer_t *t)
{
  major_timer_tick_t *major_tick=NULL;
  long is;
  struct rb_node *n;
  ulong_t mtickv;
  long r=0,i;
  major_timer_tick_t *mt;
  struct rb_node ** p;
  list_head_t *lh;
  list_node_t *ln;

  if( !t->time_x || !t->minor_tick.time_x ) {
    return -EINVAL;
  }

  if( t->time_x <= system_ticks  ) {
    return 0;
  }

  mtickv=t->time_x-(t->time_x % CONFIG_TIMER_GRANULARITY);
  t->da.__lock=&sw_timers_list_lock;

  /* First try to locate an existing major tick or create a new one.
   */
  LOCK_SW_TIMERS_R(is);
  n=timers_rb_root.rb_node;
  while( n ) {
    mt=rb_entry(n,major_timer_tick_t,rbnode);

    if( mtickv < mt->time_x ) {
      n=n->rb_left;
    } else if( mtickv > mt->time_x )  {
      n=n->rb_right;
    } else {
      major_tick=mt;
      get_major_tick(mt);
      break;
    }
  }
  UNLOCK_SW_TIMERS_R(is);

  /* No major tick for target time point, so we should create a new one.
   */
  if( !major_tick ) {
    major_tick=memalloc(sizeof(*major_tick));

    if( !major_tick ) {
      r=-ENOMEM;
      goto out;
    }

    /* Initialize a new entry. */
    atomic_set(&major_tick->use_counter,1);
    major_tick->time_x=mtickv;
    spinlock_initialize(&major_tick->lock);

    for( i=0;i<MINOR_TICK_GROUPS;i++ ) {
      list_init_head(&major_tick->minor_ticks[i]);
    }

    /* Now insert new tick into RB tree. */
    LOCK_SW_TIMERS(is);
    if( t->time_x > system_ticks ) {
      p=&timers_rb_root.rb_node;
      n=NULL;

      while( *p ) {
        n=*p;
        mt=rb_entry(n,major_timer_tick_t,rbnode);

        if( mtickv < mt->time_x ) {
          p=&(*p)->rb_left;
        } else if( mtickv > mt->time_x ) {
          p=&(*p)->rb_right;
        } else {
          /* Hmmmm. Someone else populated the same major tick concurrently. */
          get_major_tick(mt);
          goto major_tick_found;
        }
      }
      get_major_tick(major_tick); /* Add one extra reference. */
      mt=major_tick;
      rb_link_node(&major_tick->rbnode,n,p);
      rb_insert_color(&major_tick->rbnode,&timers_rb_root);
    } else {
      /* Expired while allocating a new major tick. */
      UNLOCK_SW_TIMERS(is);
      goto out;
    }
  major_tick_found:
    UNLOCK_SW_TIMERS(is);
  } else {
    mt=major_tick;
  }

  /* OK, our major tick was located so we can add our timer to it.
   */
  t->minor_tick.major_tick=mt;

  LOCK_MAJOR_TIMER_TICK(mt,is);
  lh=&mt->minor_ticks[(t->time_x-mtickv)/MINOR_TICK_GROUP_SIZE];
  if( !list_is_empty(lh) ) {
    list_for_each( lh,ln ) {
      timer_tick_t *tt=container_of(ln,timer_tick_t,node);

      if( tt->time_x == t->time_x ) {
        skiplist_add(&t->da,&tt->actions,deffered_irq_action_t,node,head,priority);
        goto out_insert;
      } else if( tt->time_x > t->time_x ) {
        list_insert_before(&t->minor_tick.node,ln);
        goto out_insert;
      }
      /* Fallthrough in case of the highest tick value - it will be added to
       * the end of the list.
       */
    }
  }
  /* By default - add this timer to the end of the list. */
  list_add2tail(lh,&t->minor_tick.node);
  list_add2tail(&t->minor_tick.actions,&t->da.node);

out_insert:
  t->minor_tick.major_tick=mt;
  UNLOCK_MAJOR_TIMER_TICK(mt,is);
  r=0;
out:
  if( major_tick != NULL ) {
    put_major_tick(major_tick);
  }
  return r;
}

static bool __timer_deffered_sched_handler(void *data)
{
  ktimer_t *timer=(ktimer_t *)data;

  return ( !(timer->da.flags & __DEF_ACT_FIRED_MASK) &&
           timer->time_x > system_ticks);
}

long sleep(ulong_t ticks)
{
  ktimer_t timer;
  long r;

  if( !ticks ) {
    return 0;
  }

  init_timer(&timer,system_ticks+ticks,DEF_ACTION_UNBLOCK);
  timer.da.d.target=current_task();

  r=add_timer(&timer);
  if( !r ) {
    sched_change_task_state_deferred(current_task(),TASK_STATE_SLEEPING,
                                     __timer_deffered_sched_handler,&timer);
    
    if( task_was_interrupted(current_task()) ) {
      r=-EINTR;
    }
  } else if( r > 0 ) { /* Expired. */
    r=0;
  }

  return r;
}
