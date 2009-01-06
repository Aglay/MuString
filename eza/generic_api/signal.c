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
 * eza/generic_api/signal.c: generic code of kernel signal delivery subsystem.
 */

#include <eza/arch/types.h>
#include <eza/task.h>
#include <eza/process.h>
#include <eza/errno.h>
#include <eza/arch/context.h>
#include <kernel/vm.h>
#include <eza/signal.h>
#include <mm/slab.h>
#include <mm/pfalloc.h>
#include <eza/security.h>

static memcache_t *sigq_cache;

#define __alloc_sigqueue_item()  alloc_from_memcache(sigq_cache)

struct __def_sig_data {
  sigset_t *blocked;
  int sig;
};

static bool __deferred_sig_check(void *d)
{
  struct __def_sig_data *sd=(struct __def_sig_data *)d;
  return !signal_matches(sd->blocked,sd->sig) &&
    signal_matches(&current_task()->siginfo.pending,sd->sig);
}

void initialize_signals(void)
{
  sigq_cache = create_memcache( "Sigqueue item memcache", sizeof(sigq_item_t),
                                2, SMCF_PGEN);
  if( !sigq_cache ) {
    panic( "initialize_signals(): Can't create the sigqueue item memcache !" );
  }
}

static bool __update_pending_signals(task_t *task)
{
  signal_struct_t *siginfo=&task->siginfo;
  bool delivery_needed;

  if( deliverable_signals_present(siginfo) ) {
    set_task_signals_pending(task);
    delivery_needed=true;
  } else {
    clear_task_signals_pending(task);
    delivery_needed=false;
  }
  return delivery_needed;
}

bool update_pending_signals(task_t *task)
{
  bool r;

  LOCK_TASK_SIGNALS(task);
  r=__update_pending_signals(task);
  UNLOCK_TASK_SIGNALS(task);

  return r;
}

/* NOTE: Caller must hold the signal lock !
 * Return codes:
 *   0: signal was successfully queued.
 *   1: signal wasn't queued since it was ignored.
 * -ENOMEM: no memory for a new queue item.
 */
static status_t __send_task_siginfo(task_t *task,siginfo_t *info,
                                    bool force_delivery)
{
  int sig=info->si_signo;
  status_t r;
  bool send_signal;

  if( force_delivery ) {
    sa_sigaction_t act=task->siginfo.handlers->actions[sig].a.sa_sigaction;

    if( act == SIG_IGN ) {
      task->siginfo.handlers->actions[sig].a.sa_sigaction=SIG_DFL;
    }
    send_signal=true;
  } else {
    send_signal=!signal_matches(&task->siginfo.ignored,sig);
  }

  /* Make sure only one instance of a non-RT signal is present. */
  if( !rt_signal(sig) && signal_matches(&task->siginfo.pending,sig) ) {
    return 0;
  }

  if( send_signal ) {
    sigq_item_t *qitem=__alloc_sigqueue_item();

    if( qitem ) {
      qitem->h.idx=sig;
      qitem->info=*info;

      sigqueue_add_item(&task->siginfo.sigqueue,&qitem->h);
      sigaddset(&task->siginfo.pending,sig);
      r=0;
    } else {
      r=-ENOMEM;
    }
  } else {
    r=1;
  }
  return r;
}

static void __send_siginfo_postlogic(task_t *task,siginfo_t *info)
{
  if( update_pending_signals(task) && task != current_task() ) {
    /* Need to wake up the receiver. */
    struct __def_sig_data sd;
    ulong_t state=TASK_STATE_SLEEPING | TASK_STATE_STOPPED;

    sd.blocked=&task->siginfo.blocked;
    sd.sig=info->si_signo;
    sched_change_task_state_deferred_mask(task,TASK_STATE_RUNNABLE,
                                          __deferred_sig_check,&sd,
                                          state);
  }
}

status_t send_task_siginfo(task_t *task,siginfo_t *info,bool force_delivery)
{
  status_t r;

  LOCK_TASK_SIGNALS(task);
  r=__send_task_siginfo(task,info,force_delivery);
  UNLOCK_TASK_SIGNALS(task);

  if( !r ) {
    __send_siginfo_postlogic(task,info);
  } else if( r == 1 ) {
    kprintf( "send_task_siginfo(): Ignoring signal %d for %d=%d\n",
             info->si_signo,task->pid,task->tid);
  }
  return r < 0 ? r : 0;
}

status_t static __send_pid_siginfo(siginfo_t *info,pid_t pid)
{
  task_t *caller=current_task();
  task_t *task;
  int sig=info->si_signo;
  status_t r;

  if( !pid ) {
    return -2;
  } else if ( pid > 0 ) {
    if( is_tid(pid) ) { /* Send signal to a separate thread. */
      /* Make sure target thread belongs to our process. */
      if( caller->pid != TID_TO_PIDBASE(pid) ) {
        return -EINVAL;
      }
      task=pid_to_task(pid);
      if( !task ) {
        return -ESRCH;
      }
      if( !process_wide_signal(sig) ) {
        /* OK, send signal to target thread. */
        r=send_task_siginfo(task,info,false);
        release_task_struct(task);
        return r;
      } else {
        /* Trying to send a process-wide signal, so fall-through. */
        release_task_struct(task);
      }
    }
    /* Send signal to a whole process. */
    return -5;
  } else {
    return -10;
  }
}

status_t sys_kill(pid_t pid,int sig,siginfo_t *sinfo)
{
  status_t r;
  siginfo_t k_siginfo;

  if( !valid_signal(sig) ) {
    kprintf("sys_kill: bad signal %d!\n",sig);
    return -EINVAL;
  }

  if( sinfo ) {
    if( !trusted_task(current_task()) ) {
      return -EPERM;
    }

    if( copy_from_user(&k_siginfo,sinfo,sizeof(k_siginfo)) ) {
      return -EFAULT;
    }
  } else {
    memset(&k_siginfo,0,sizeof(k_siginfo));
  }

  k_siginfo.si_signo=sig;
  k_siginfo.si_errno=0;
  k_siginfo.si_pid=current_task()->pid;
  k_siginfo.si_uid=current_task()->uid;
  k_siginfo.si_code=SI_USER;

  r=__send_pid_siginfo(&k_siginfo,pid);
  kprintf( ">> sending signal to %d : %d\n",
           pid,r);
  return r;
}

status_t sys_thread_kill(pid_t process,tid_t tid,int sig)
{
  task_t *target=pid_to_task(tid);
  status_t r;
  siginfo_t k_siginfo;

  if( !target ) {
    return -ESRCH;
  }

  if( !valid_signal(sig) ) {
    return -EINVAL;
  }

  if( target->pid != process ) {
    r=-ESRCH;
    goto out;
  }

  memset(&k_siginfo,0,sizeof(k_siginfo));
  k_siginfo.si_signo=sig;
  k_siginfo.si_errno=0;
  k_siginfo.si_pid=current_task()->pid;
  k_siginfo.si_uid=current_task()->uid;
  k_siginfo.si_code=SI_USER;

  r=send_task_siginfo(target,&k_siginfo,false);
out:
  release_task_struct(target);
  return r;
}

static status_t sigaction(kern_sigaction_t *sact,kern_sigaction_t *oact,
                          int sig) {
  task_t *caller=current_task();
  sa_sigaction_t s=sact->a.sa_sigaction;
  sq_header_t *removed_signals=NULL;

  if( !valid_signal(sig) ) {
    return -EINVAL;
  }

  /* Remove signals that can't be blocked. */
  sact->sa_mask &= ~UNTOUCHABLE_SIGNALS;

  LOCK_TASK_SIGNALS(caller);
  if( oact ) {
    *oact=caller->siginfo.handlers->actions[sig];
  }
  caller->siginfo.handlers->actions[sig]=*sact;

  /* POSIX 3.3.1.3 */
  if( s == SIG_IGN || (s == SIG_DFL && def_ignorable(sig)) ) {
    sigaddset(&caller->siginfo.ignored,sig);

    __update_pending_signals(caller);
    removed_signals=sigqueue_remove_item(&caller->siginfo.sigqueue,sig,true);
  } else {
    sigdelset(&caller->siginfo.ignored,sig);
  }
  UNLOCK_TASK_SIGNALS(caller);

  /* Now we can sefely remove queue items. */
  if( removed_signals != NULL ) {
    list_node_t *last=removed_signals->l.prev;
    list_node_t *next=&removed_signals->l;

    do {
      sq_header_t *h=container_of(next,sq_header_t,l);
      next=next->next;
      free_sigqueue_item(h);
    } while(last != next);
  }
  return 0;
}

status_t sys_signal(int sig,sa_handler_t handler)
{
  kern_sigaction_t act,oact;
  status_t r;

  if( (sig == SIGKILL || sig == SIGSTOP) && (sa_sigaction_t)handler != SIG_DFL ) {
    return -EINVAL;
  }

  act.a.sa_handler=handler;
  act.sa_flags=SA_RESETHAND | SA_NODEFER;
  sigemptyset(act.sa_mask);

  r=sigaction(&act,&oact,sig);
  kprintf( "sys_signal(%d,%p): %d\n",
           sig,handler,r);
  return !r ? (status_t)oact.a.sa_sigaction : r;
}

status_t sys_sigaction(int signum,sigaction_t *act,
                       sigaction_t *oldact)
{
  kern_sigaction_t kact,koact;
  sigaction_t uact;
  status_t r;

  if( !valid_signal(signum) || signum == SIGKILL ||
      signum == SIGSTOP ) {
    return -EINVAL;
  }

  if( !act ) {
    return -EFAULT;
  }

  if( copy_from_user(&uact,act,sizeof(uact)) ) {
    return -EFAULT;
  }

  /* Transform userspace data to kernel data. */
  if( uact.sa_flags & SA_SIGINFO ) {
    kact.a.sa_sigaction=uact.sa_sigaction;
  } else {
    kact.a.sa_handler=uact.sa_handler;
  }
  if( !kact.a.sa_handler ) {
    return -EINVAL;
  }

  kact.sa_mask=uact.sa_mask;
  kact.sa_flags=uact.sa_flags;

  r=sigaction(&kact,oldact ? &koact : NULL,signum);

  if( !r && oldact ) {
    if( copy_to_user(oldact,&koact,sizeof(koact)) ) {
      r=-EFAULT;
    }
  }
  return r;
}

sighandlers_t *allocate_signal_handlers(void)
{
  sighandlers_t *sh=alloc_pages_addr(1,AF_PGEN);

  if( sh ) {
    int i;

    memset(sh,0,sizeof(*sh));
    spinlock_initialize(&sh->lock);
    atomic_set(&sh->use_count,1);

    /* Now setup default signal actions. */
    for(i=0;i<NR_SIGNALS;i++) {
      sh->actions[i].a.sa_sigaction=(_BM(i) & DEFAULT_IGNORED_SIGNALS) ? SIG_IGN : SIG_DFL;
    }
  }
  return sh;
}

sigq_item_t *extract_one_signal_from_queue(task_t *task)
{
  sq_header_t *sh;

  LOCK_TASK_SIGNALS(task);
  sh=sigqueue_remove_first_item(&task->siginfo.sigqueue,false);
  UNLOCK_TASK_SIGNALS(task);

  if( sh != NULL ) {
    return container_of(sh,sigq_item_t,h);
  }
  return NULL;
}
