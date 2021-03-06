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
 * ipc/prio_port.c: Implementation of IPC ports that provide prioritized message
 *                  queues (based on static priorities of clients).
 *
 */

#include <arch/types.h>
#include <ipc/port.h>
#include <mstring/task.h>
#include <mstring/errno.h>
#include <mm/page_alloc.h>
#include <mm/page.h>
#include <ds/idx_allocator.h>
#include <ipc/ipc.h>
#include <ipc/buffer.h>
#include <mm/slab.h>
#include <ipc/port.h>
#include <ds/list.h>
#include <ds/skiplist.h>

typedef struct __prio_port_data_storage {
  list_head_t prio_head,all_messages,id_waiters;
  ipc_port_message_t **message_ptrs;
  idx_allocator_t msg_array;
  ulong_t num_waiters;
} prio_port_data_storage_t;

#define __remove_message(msg)  skiplist_del((msg),ipc_port_message_t,h,l)

static int prio_init_data_storage(struct __ipc_gen_port *port,
                                  task_t *owner,ulong_t queue_size)
{
  prio_port_data_storage_t *ds;
  int r;

  if( port->data_storage ) {
    return -EBUSY;
  }

  ds=memalloc(sizeof(*ds));
  if( !ds ) {
    return -ENOMEM;
  }

  ds->message_ptrs=allocate_ipc_memory(queue_size*sizeof(ipc_port_message_t*));
  if( ds->message_ptrs == NULL ) {
    goto free_ds;
  }

  r=idx_allocator_init(&ds->msg_array,queue_size);
  if( r ) {
    goto free_messages;
  }

  list_init_head(&ds->prio_head);
  list_init_head(&ds->all_messages);
  list_init_head(&ds->id_waiters);
  ds->num_waiters=0;
  port->data_storage=ds;
  port->capacity=queue_size;
  return 0;
free_messages:
  free_ipc_memory(ds->message_ptrs,queue_size*sizeof(ipc_port_message_t*));  
free_ds:
  memfree(ds);
  return -ENOMEM;
}

static void __add_one_message(list_head_t *list,
                              ipc_port_message_t *msg)
{
  if( list_is_empty(list) ) {
    list_add2head(list,&msg->l);
  } else {
    list_node_t *n;
    ulong_t p2=msg->sender->static_priority;

    list_for_each(list,n) {
      ipc_port_message_t *m=container_of(n,ipc_port_message_t,l);
      ulong_t p1=m->sender->static_priority;

      if( p1 > p2 ) {
        list_add_before(&m->l, &msg->l);
        return;
      } else if( p1 == p2 ) {
        list_add2tail(&m->h,&msg->l);
        return;
      }
    }
    list_add2tail(list,&msg->l);
  }
}

static int prio_insert_message(struct __ipc_gen_port *port,
                                   ipc_port_message_t *msg)
{
  prio_port_data_storage_t *ds=(prio_port_data_storage_t *)port->data_storage;
  ulong_t id=idx_allocate(&ds->msg_array);

  port->avail_messages++;
  port->total_messages++;
  list_init_head(&msg->h);
  list_init_node(&msg->l);
  list_add2tail(&ds->all_messages,&msg->messages_list);

  if( id != IDX_INVAL ) { /* Insert this message in the array directly. */
    ds->message_ptrs[id]=msg;
    __add_one_message(&ds->prio_head,msg);
  } else { /* No free slots - put this message to the waitlist. */
    id=WAITQUEUE_MSG_ID;
    ds->num_waiters++;
    __add_one_message(&ds->id_waiters,msg);
  }
  msg->id=id;

  return 0;
}

static ipc_port_message_t *prio_extract_message(ipc_gen_port_t *p,ulong_t flags)
{
  prio_port_data_storage_t *ds=(prio_port_data_storage_t*)p->data_storage;

  if( !list_is_empty(&ds->prio_head) ) {
    ipc_port_message_t *msg=container_of(list_node_first(&ds->prio_head),
                                         ipc_port_message_t,l);
    __remove_message(msg);
    p->avail_messages--;
    return msg;
  }
  return NULL;
}

static int prio_remove_message(struct __ipc_gen_port *port,
                                    ipc_port_message_t *msg)
{
  prio_port_data_storage_t *ds=(prio_port_data_storage_t*)port->data_storage;

  if( !list_node_is_bound(&msg->messages_list) ) {
    return -EINVAL;
  }

  if( msg->id < port->capacity ) {
    if( list_node_is_bound(&msg->l) ) {
      port->avail_messages--;
    }

    /* Check if there are tasks waiting for a free message slot. */
    if( ds->num_waiters ) {
      ipc_port_message_t *m=container_of(list_node_first(&ds->id_waiters),
                                         ipc_port_message_t,l);
      m->id=msg->id;
      ds->message_ptrs[m->id]=m;

      __remove_message(m);
      __add_one_message(&ds->prio_head,m);
      ds->num_waiters--;
    } else {
      idx_free(&ds->msg_array,msg->id);
      ds->message_ptrs[msg->id]=NULL;
    }
  } else if( msg->id == WAITQUEUE_MSG_ID ) {
    ds->num_waiters--;
    port->avail_messages--;
  } else {
    return -EINVAL;
  }

  port->total_messages--;
  list_del(&msg->messages_list);

  if( list_node_is_bound(&msg->l) ) {
    __remove_message(msg);
  }

  return 0;
}

static ipc_port_message_t *prio_remove_head_message(struct __ipc_gen_port *port)
{
  prio_port_data_storage_t *ds=(prio_port_data_storage_t*)port->data_storage;

  if( !list_is_empty(&ds->all_messages) ) {
    ipc_port_message_t *msg=container_of(list_node_first(&ds->all_messages),
                                         ipc_port_message_t,messages_list);
    prio_remove_message(port,msg);
    return msg;
  }
  return NULL;
}

static void prio_dequeue_message(struct __ipc_gen_port *port,
                                 ipc_port_message_t *msg)
{
  prio_port_data_storage_t *ds=(prio_port_data_storage_t*)port->data_storage;

  if( !list_node_is_bound(&msg->messages_list) ) {
    return;
  }

  if( msg->id < port->capacity ) {
    if( list_node_is_bound(&msg->l) ) {
      list_del(&msg->l);
      port->avail_messages--;
    }
    ds->message_ptrs[msg->id]=__MSG_WAS_DEQUEUED;
  } else if( msg->id == WAITQUEUE_MSG_ID ) { /* This message belongs to the waitlist. */
    ASSERT(list_node_is_bound(&msg->l));
    list_del(&msg->l);

    ds->num_waiters--;
    port->total_messages--;
    port->avail_messages--;
  }

  list_del(&msg->messages_list);
}

static ipc_port_message_t *prio_lookup_message(struct __ipc_gen_port *port,
                                               ulong_t msg_id)
{    
  if( msg_id < port->capacity ) {
    prio_port_data_storage_t *ds=(prio_port_data_storage_t*)port->data_storage;
    ipc_port_message_t *msg=ds->message_ptrs[msg_id];

    if( msg == __MSG_WAS_DEQUEUED ) { /* Deferred cleanup. */
      ds->message_ptrs[msg_id]=NULL;
      port->total_messages--;
      idx_free(&ds->msg_array,msg_id);
    }
    return msg;
  }

  return NULL;
}

static void prio_destructor(ipc_gen_port_t *port)
{
  prio_port_data_storage_t *ds=(prio_port_data_storage_t*)port->data_storage;

  free_ipc_memory(ds->message_ptrs,port->capacity*sizeof(ipc_port_message_t*));
  idx_allocator_destroy(&ds->msg_array);
  memfree(ds);
}

ipc_port_msg_ops_t prio_port_msg_ops = {
  .init_data_storage=prio_init_data_storage,
  .insert_message=prio_insert_message,
  .extract_message=prio_extract_message,
  .remove_message=prio_remove_message,
  .remove_head_message=prio_remove_head_message,
  .dequeue_message=prio_dequeue_message,
  .lookup_message=prio_lookup_message,
};

ipc_port_ops_t prio_port_ops = {
  .destructor=prio_destructor,
};
