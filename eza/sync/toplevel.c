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
 * eza/sync/topevel.c: Core logic for userspace synchronization subsystem.
 */

#include <eza/arch/types.h>
#include <eza/mutex.h>
#include <eza/sync.h>
#include <kernel/vm.h>
#include <eza/task.h>
#include <mm/slab.h>

static status_t __sync_allocate_id(struct __task_struct *task,
                                   bool shared_id,sync_id_t *p_id)
{
  static sync_id_t __id=0;

  *p_id=++__id;
  return 0;
}

static void __sync_free_id(struct __task_struct *task,sync_id_t id)
{
}

static void __install_sync_object(task_t *task,kern_sync_object_t *obj,
                                  sync_id_t id)
{
  obj->id=id;
  task->sync_data->sync_objects[id]=obj;
  task->sync_data->numobjs++;
}

static kern_sync_object_t *__lookup_sync(task_t *owner,sync_id_t id)
{
  kern_sync_object_t *t=NULL;
  task_sync_data_t *tsd=get_task_sync_data(owner);

  if( !tsd ) {
    return NULL;
  }

  LOCK_SYNC_DATA_R(tsd);
  if( id < MAX_PROCESS_SYNC_OBJS ) {
    t=tsd->sync_objects[id];
    if( t ) {
      sync_get_object(t);
    }
  }

  UNLOCK_SYNC_DATA_R(tsd);
  release_task_sync_data(tsd);
  return t;
}

status_t sys_sync_create_object(sync_object_type_t obj_type,
                                void *uobj,uint8_t *attrs,
                                ulong_t flags)
{
  kern_sync_object_t *kobj=NULL;
  status_t r;
  sync_id_t id;
  task_t *caller=current_task();
  uint8_t shared;
  task_sync_data_t *sync_data;

  if( !uobj || obj_type > __SO_MAX_TYPE ) {
    return -EINVAL;
  }

  if( !attrs ) {
    shared=0;
  } else {
    if( get_user(shared,attrs) ) {
      return -EINVAL;
    }
    if( shared != PTHREAD_PROCESS_PRIVATE &&
        shared != PTHREAD_PROCESS_SHARED ) {
      return -EINVAL;
    }
  }

  sync_data=get_task_sync_data(caller);
  if( !sync_data ) {
    return -EAGAIN;
  }

  LOCK_SYNC_DATA_W(sync_data);
  r=__sync_allocate_id(caller,shared,&id);
  if( r ) {
    goto put_sync_data;
  }
  UNLOCK_SYNC_DATA_W(sync_data);

  switch( obj_type ) {
    case __SO_MUTEX:
      r=sync_create_mutex(&kobj,uobj,attrs,flags);
      break;
    case __SO_RAWEVENT:
      r=sync_create_uevent(&kobj,uobj,attrs,flags);
      break;
    case __SO_CONDVAR:
    case __SO_SEMAPHORE:
    default:
      r=-EINVAL;
      break;
  }

  /* Copy ID to user without holding the lock for better performance. */
  if( !r ) {
    r=copy_to_user(uobj,&id,sizeof(id));
  }

  LOCK_SYNC_DATA_W(sync_data);
  if( r ) {
    goto put_id;
  }
  __install_sync_object(caller,kobj,id);
  UNLOCK_SYNC_DATA_W(sync_data);

  return 0;
put_id:
  __sync_free_id(caller,id);
put_sync_data:
  UNLOCK_SYNC_DATA_W(sync_data);

  /* Free object, if any. */
  if( kobj ) {
    sync_put_object(kobj);
  }
  release_task_sync_data(sync_data);
  return r;
}

status_t sys_sync_control(sync_id_t id,ulong_t cmd,ulong_t arg)
{
  task_t *caller=current_task();
  kern_sync_object_t *kobj=__lookup_sync(caller,id);
  status_t r;

  if( !kobj ) {
    return -EINVAL;
  }

  r=kobj->ops->control(kobj,cmd,arg);
  sync_put_object(kobj);
  return r;
}

status_t sys_sync_destroy(sync_id_t id)
{
  task_t *caller=current_task();
  kern_sync_object_t *kobj=__lookup_sync(caller,id);

  if( !kobj ) {
    return -EINVAL;
  }

  return 0;
}

status_t sys_sync_condvar_wait(sync_id_t ucondvar,
                               sync_id_t umutex)
{
  return 0;
}

status_t dup_task_sync_data(task_sync_data_t *sync_data)
{
  ulong_t i;

  atomic_inc(&sync_data->use_count);
  LOCK_SYNC_DATA_R(sync_data);

  for(i=0;i<MAX_PROCESS_SYNC_OBJS;i++) {
    kern_sync_object_t *kobj=sync_data->sync_objects[i];

    if( kobj ) {
      sync_get_object(kobj);
    }
  }

  UNLOCK_SYNC_DATA_R(sync_data);
  return 0;
}

task_sync_data_t *allocate_task_sync_data(void)
{
  task_sync_data_t *s=memalloc(sizeof(*s));

  if( s ) {
    memset(s,0,sizeof(*s));
    atomic_set(&s->use_count,1);
    mutex_initialize(&s->mutex);
  }

  return s;
}

void sync_default_dtor(void *obj)
{
  memfree(obj);
}