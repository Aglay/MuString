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
 * eza/generic_api/tevent.c: implementation of functions related to task events.
 */

#include <mlibc/types.h>
#include <kernel/syscalls.h>
#include <eza/spinlock.h>
#include <eza/errno.h>
#include <mm/slab.h>
#include <ds/list.h>
#include <eza/task.h>
#include <ipc/port.h>
#include <ipc/ipc.h>

#define __free_listener(l)  memfree(l)

static task_event_listener_t *__alloc_listener(void)
{
  task_event_listener_t *l=memalloc(sizeof(*l));

  if( l ) {
    list_init_node(&l->owner_list);
    list_init_node(&l->llist);
    l->port=NULL;
  }

  return l;
}

static void __release_listener(task_event_listener_t *l)
{
  ipc_put_port(l->port);
  __free_listener(l);
}

void task_event_notify(ulong_t events)
{
  task_t *task=current_task();

  LOCK_TASK_EVENTS(task);
  if( !list_is_empty(&task->task_events.listeners) ) {
    list_node_t *n;
    task_event_descr_t e;
    iovec_t iov;

    e.pid=task->pid;
    e.tid=task->tid;

    iov.iov_base=&e;
    iov.iov_len=sizeof(e);

    list_for_each(&task->task_events.listeners,n) {
      task_event_listener_t *l=container_of(n,task_event_listener_t,llist);

      if( l->events & events ) {
        iovec_t iov;

        e.ev_mask=l->events & events;
        iov.iov_base=&e;
        iov.iov_len=sizeof(e);

        ipc_port_message_t *msg=ipc_create_port_message_iov_v(&iov,1,sizeof(e),false,
                                                              NULL,0,NULL,NULL,0);
        if( msg ) {
          ipc_port_send_iov(l->port,msg,false,NULL,0,0);
        }
      }
    }
  }
  UNLOCK_TASK_EVENTS(task);
}

int task_event_attach(task_t *target,task_t *listener,
                      task_event_ctl_arg *ctl_arg)
{
  ipc_gen_port_t *port;
  task_event_listener_t *l;
  int r=-EINVAL;
  list_node_t *n;

  if( !ctl_arg->ev_mask || (ctl_arg->ev_mask & ~(ALL_TASK_EVENTS_MASK)) ) {
    return -EINVAL;
  }

  l=__alloc_listener();
  if( !l ) {
    return -ENOMEM;
  }

  if( !(port=ipc_get_port(listener,ctl_arg->port)) ) {
    goto out_free;
  }

  if( port->flags & IPC_BLOCKED_ACCESS ) {
    goto put_port;
  }

  l->port=port;
  l->events=ctl_arg->ev_mask;
  l->listener=listener;
  l->target=target;

  /* Make sure caller hasn't installed another listenersfor this process. */
  LOCK_TASK_EVENTS(target);
  if( check_task_flags(target,TF_EXITING) ) {
    /* Target task became a zombie ? */
    r=-ESRCH;
    goto dont_add;
  }

  list_for_each(&target->task_events.listeners,n) {
    task_event_listener_t *tl=container_of(n,task_event_listener_t,llist);

    if( tl->listener == listener ) {
      r=-EBUSY;
      goto dont_add;
    }
  }
  list_add2tail(&target->task_events.listeners,&l->llist);
  grab_task_struct(target);
  r=0;
dont_add:
  UNLOCK_TASK_EVENTS(target);

  if( r ) {
    goto put_port;
  }

  /* Add this listener to our list. */
  list_add2tail(&listener->task_events.my_events,&l->owner_list);

  return r;
put_port:
  ipc_put_port(port);
out_free:
  __free_listener(l);
  return r;
}

int task_event_detach(int target,struct __task_struct *listener)
{
  list_head_t lss;
  list_node_t *iter,*safe;
  int evcount=0;
  task_event_listener_t *tl;
  task_t *__t;

  list_init_head(&lss);
  list_for_each_safe(&listener->task_events.my_events,iter,safe) {
    tl=container_of(iter,task_event_listener_t,owner_list);

    if( tl->target->pid == target ) {
      LOCK_TASK_EVENTS(tl->target);
      /* Target can exit before we've reached the list node, and remove us from its
       * list of attached actions, so perform an extra check.
       */
      if( list_node_is_bound(&tl->llist) ) {
        list_del(&tl->llist);
      }
      list_add2tail(&lss,&tl->llist);
      UNLOCK_TASK_EVENTS(tl->target);
      evcount++;
      __t=tl->target;
    }
  }

  list_for_each_safe(&lss,iter,safe) {
    tl=list_entry(iter,task_event_listener_t,llist);

    list_del(&tl->owner_list);
    release_task_struct(__t);
    __release_listener(tl);
  }

  return (evcount ? 0 : -EINVAL);
}

void exit_task_events(task_t *target)
{
  list_node_t *n,*ns;
  task_event_listener_t *tl;

  /* Step 1: Remove our listeners. */
  list_for_each_safe(&target->task_events.my_events,n,ns) {
    tl=container_of(n,task_event_listener_t,owner_list);

    list_del(&tl->owner_list);
    LOCK_TASK_EVENTS(tl->target);

    /* Target can exit before we've reached the list node, and remove us from its
     * list of attached actions, so perform an extra check.
     */
    if( list_node_is_bound(&tl->llist) ) {
      list_del(&tl->llist);
    }
    UNLOCK_TASK_EVENTS(tl->target);

    release_task_struct(tl->target);
    __release_listener(tl);
  }

  /* Step 2: Remove listeners that target us. */
  LOCK_TASK_EVENTS(target);
  list_for_each_safe(&target->task_events.listeners,n,ns) {
    list_del(n);
  }
  UNLOCK_TASK_EVENTS(target);
}
