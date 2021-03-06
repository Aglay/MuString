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
 * arch/amd64/signal.c: AMD64-specific code for signal delivery.
 */

#include <mstring/types.h>
#include <mstring/kprintf.h>
#include <mstring/smp.h>
#include <mstring/task.h>
#include <arch/context.h>
#include <mstring/signal.h>
#include <mstring/errno.h>
#include <arch/current.h>
#include <mstring/process.h>
#include <mstring/timer.h>
#include <mstring/posix.h>
#include <mstring/kprintf.h>
#include <mstring/usercopy.h>
#include <config.h>
#include <mstring/gc.h>
#include <mstring/ptrace.h>

#define USPACE_TRMPL(a) USPACE_ADDR((uintptr_t)(a),__utrampoline_virt)

uintptr_t __utrampoline_virt;

struct __trampoline_ctx {
  uint64_t handler,arg1,arg2,arg3;
};

struct signal_context {
  struct __trampoline_ctx trampl_ctx;
  struct gpregs gpr_regs;
  uint8_t xmm_ctx[XMM_CTX_SIZE] __aligned(16);
  usiginfo_t siginfo;
  sigset_t saved_blocked;
  long retcode;
  uintptr_t retaddr;
};

/* Userspace trampolines */
extern void trampoline_sighandler_invoker_int(void);
extern void trampoline_cancellation_invoker(void);

static int __setup_trampoline_ctx(struct signal_context *__user ctx,
                                  usiginfo_t *siginfo,sa_sigaction_t act)
{
  struct __trampoline_ctx kt;

  if( copy_to_user(&ctx->siginfo,siginfo,sizeof(*siginfo) ) ) {
    return -EFAULT;
  }

  kt.handler=(uint64_t)act;
  kt.arg1=siginfo->si_signo;
  kt.arg2=(uint64_t)&ctx->siginfo;
  kt.arg3=0;

  if( copy_to_user(&ctx->trampl_ctx.handler,&kt,sizeof(kt)) ) {
    return -EFAULT;
  }

  return 0;
}

static void __perform_default_action(int sig)
{
  task_t *t = current_task();

#ifdef CONFIG_DEBUG_SIGNALS
  kprintf("[!!] [%d:%d] Default action for signal %d is: ",
          current_task()->pid,current_task()->tid,sig);
#endif

  if( is_lethal_signal(sig) ) {
#ifdef CONFIG_DEBUG_SIGNALS
    kprintf("TERMINATE\n");
#endif
    t->last_signum = sig;
    do_exit(0,0,0);
  }

#ifdef CONFIG_DEBUG_SIGNALS
  kprintf("IGNORE\n");
#endif

  if (sig == SIGSTOP) {
    LOCK_TASK_STRUCT(t);

    sched_change_task_state(t, TASK_STATE_STOPPED);

    if (task_traced(t))
      set_ptrace_event(t, PTRACE_EV_STOPPED);

    t->wstat = WSTAT_STOPPED;
    // do not overwrite a lethal signal record so as in would be collected
    if (!is_lethal_signal(sig)) {
      t->last_signum = SIGSTOP;
    }

    /*
     * It's needed to atomicaly change state to avoid spurious
     * ptrace calls. In the other hand one should not to wakeup
     * under hold lock to avoid deadlock.
     */
    preempt_disable();
    UNLOCK_TASK_STRUCT(t);

    wakeup_waiters(t);
    preempt_enable();
  }
}

static void __handle_cancellation_request(int reason,uintptr_t kstack)
{
  uintptr_t extra_bytes;
  struct intr_stack_frame *int_frame;
  struct gpregs *kpregs;

  switch( reason ) {
    case __SYCALL_UWORK:
    case __INT_UWORK:
      extra_bytes=0;
      break;
    case __XCPT_ERR_UWORK:
      extra_bytes=8;
      break;
  }

  /* See '__handle_pending_signals()' for detailed descriptions of
   * the following stack manipulations.
   */
  kpregs=(struct gpregs *)kstack;
  kstack=(uintptr_t)kpregs + sizeof(*kpregs);
  kstack += extra_bytes;
  int_frame=(struct intr_stack_frame *)kstack;

  /* Prepare cancellation context. */
  kpregs->rdi=current_task()->uworks_data.destructor;
  int_frame->rip=USPACE_TRMPL(trampoline_cancellation_invoker);
}

static int __setup_int_context(uint64_t retcode,uintptr_t kstack,
                               usiginfo_t *info,sa_sigaction_t act,
                               ulong_t extra_bytes,
                               struct signal_context **pctx)
{
  uintptr_t ustack,t;
  struct intr_stack_frame *int_frame;
  struct gpregs *kpregs;
  struct signal_context *ctx;
  uint8_t xmm_ctx[XMM_CTX_SIZE + XMM_ALIGNMENT];

  /* Locate saved GPRs. */
  kpregs=(struct gpregs *)kstack;
  kstack+=sizeof(*kpregs);

  /* OK, now we're pointing at saved interrupt number (see asm.S).
   * So skip it.
   */

  fxsave(align_up((uintptr_t)xmm_ctx, XMM_ALIGNMENT));
  /* Now we can access hardware interrupt stackframe. */
  kstack += extra_bytes;
  int_frame=(struct intr_stack_frame *)kstack;
  /* Now we can create signal context. */
  ctx=(struct signal_context *)(int_frame->rsp-sizeof(*ctx));
  if( copy_to_user(&ctx->gpr_regs,kpregs,sizeof(*kpregs))) {
    return ERR(-EFAULT);
  }
  if (copy_to_user(&ctx->xmm_ctx, xmm_ctx, XMM_CTX_SIZE)) {
    return ERR(-EFAULT);
  }

  if( __setup_trampoline_ctx(ctx,info,act) ) {
    return ERR(-EFAULT);
  }

  if( copy_to_user(&ctx->retcode,&retcode,sizeof(retcode)) ) {
    return ERR(-EFAULT);
  }

  t=int_frame->rip;
  if( copy_to_user(&ctx->retaddr,&t,sizeof(t)) ) {
    return -EFAULT;
  }

  /* Setup trampoline. */
  int_frame->rip=USPACE_TRMPL(trampoline_sighandler_invoker_int);

  *pctx=ctx;
  ustack=(uintptr_t)ctx;
  /* Setup user stack pointer. */
  int_frame->rsp=ustack;

  return 0;
}

static void __handle_pending_signals(int reason, uint64_t retcode,
                                     uintptr_t kstack)
{
  int r;
  sigq_item_t *sigitem;
  sa_sigaction_t act;
  task_t *caller=current_task();
  kern_sigaction_t *ka;
  sigset_t sa_mask,kmask;
  int sa_flags;
  struct signal_context *pctx;

  sigitem=extract_one_signal_from_queue(caller);
  if( !sigitem ) {
    return;
  }

  /* Determine if there is a user-specific handler installed. */
  ka=&caller->siginfo.handlers->actions[sigitem->info.si_signo];
  LOCK_TASK_SIGNALS(caller);
  act=ka->a.sa_sigaction;
  sa_mask=ka->sa_mask;
  sa_flags=ka->sa_flags;
  UNLOCK_TASK_SIGNALS(caller);

  if( act == SIG_IGN ) {
    goto out_recalc;
  } else if( act == SIG_DFL ) {
    caller->last_siginfo = &sigitem->info;
    __perform_default_action(sigitem->info.si_signo);
  } else {
    switch( reason ) {
      case __SYCALL_UWORK:
      case __INT_UWORK:
        r=__setup_int_context(retcode,kstack,&sigitem->info,act,0,&pctx);
        break;
      case __XCPT_ERR_UWORK:
        r=__setup_int_context(retcode,kstack,&sigitem->info,act,8,&pctx);
        break;
      default:
        panic( "Unknown userspace work type: %d in task (%d:%d)\n",
               reason,caller->pid,caller->tid);
    }
    if( r ) {
      goto bad_memory;
    }

    /* Now we should apply a new mask of blocked signals. */
    LOCK_TASK_SIGNALS(caller);
    kmask=caller->siginfo.blocked;
    caller->siginfo.blocked=sa_mask;
    UNLOCK_TASK_SIGNALS(caller);

    if( copy_to_user(&pctx->saved_blocked,&kmask,sizeof(kmask)) ) {
      goto bad_memory;
    }
  }

  if( sigitem->kern_priv ) {
    process_sigitem_private(sigitem);
  }

out_recalc:
  update_pending_signals(current_task());
  free_sigqueue_item(sigitem);
  return;
bad_memory:
  panic("***** BAD MEMORY !!! %p: %p\n", &pctx->saved_blocked, pctx);
  return;
}

void handle_uworks(int reason, uint64_t retcode,uintptr_t kstack)
{
  ulong_t uworks=read_task_pending_uworks(current_task());
  task_t *current=current_task();
  int i;

  /* First, check for pending disintegration requests. */
  if( uworks & ARCH_CTX_UWORKS_DISINT_REQ_MASK ) {
      /*kprintf_fault("[UWORKS]: %d/%d. Processing works for %d:0x%X, KSTACK: %p\n",
                    reason,retcode,
                    current->pid,current->tid,
                    kstack);
                    kprintf_fault("[UWORKS]: UWORKS=0x%X\n",uworks);*/

    if( current->uworks_data.flags & DAF_EXIT_PENDING ) {
      do_exit(current->uworks_data.exit_value,0,0);
    } else if( current->uworks_data.flags & DAF_CANCELLATION_PENDING ) {
      __handle_cancellation_request(reason,kstack);
      clear_task_disintegration_request(current);
    } else {
      /* Disintegration request.
       * Only main threads will return to finalize their reborn.
       * There can be some signals waiting for delivery, so take it
       * into account.
       */

      perform_disintegration_work();
      uworks=read_task_pending_uworks(current);
    }
  }

  i=0;
repeat:
  uworks=read_task_pending_uworks(current_task());
  if( uworks & ARCH_CTX_UWORKS_SIGNALS_MASK ) { /* Next, check for pending signals. */
    if( i < CONFIG_MAX_DEFERRED_USERSPACE_ACTIONS ) {
      gc_action_t *a;

      LOCK_TASK_SIGNALS(current);
      if( !list_is_empty(&current->uworks_data.def_uactions) ) {
        a=container_of(list_node_first(&current->uworks_data.def_uactions),
                       gc_action_t,l);
        list_del(&a->l);
      } else {
        a=NULL;
      }
      UNLOCK_TASK_SIGNALS(current);

      if( a ) { /* Have some deferred userspace works. */
        a->action(a);
        update_pending_signals(current);
        i++;
        goto repeat;
      }
    }

    __handle_pending_signals(reason,retcode,kstack);
  }
}

long sys_sigreturn(uintptr_t ctx)
{
  struct signal_context *uctx=(struct signal_context *)ctx;
  long retcode;
  task_t *caller=current_task();
  uintptr_t kctx=(uintptr_t)caller->kernel_stack.high_address-sizeof(struct gpregs)-
                 sizeof(struct intr_stack_frame);
  struct intr_stack_frame *sframe=(struct intr_stack_frame*)((uintptr_t)caller->kernel_stack.high_address-
                                   sizeof(struct intr_stack_frame));
  uintptr_t retaddr;
  sigset_t sa_mask;
  uint8_t xmm_ctx[XMM_CTX_SIZE + XMM_ALIGNMENT];
  uintptr_t xmm_ctx_addr = align_up((uintptr_t)xmm_ctx, XMM_ALIGNMENT);
  uint32_t *xmm_reg;
  int restore_sigmask;

  if( !valid_user_address_range(ctx,sizeof(struct signal_context)) ) {
    goto bad_ctx;
  }

  /* Restore the mask of blocked signals. */
  if( copy_from_user(&sa_mask,&uctx->saved_blocked,sizeof(sa_mask)) ) {
    goto bad_ctx;
  }

  /* Don't touch these signals ! */
  sa_mask &= ~UNTOUCHABLE_SIGNALS;

  LOCK_TASK_STRUCT(caller);
  restore_sigmask = check_task_flags(caller, TF_RESTORE_SIGMASK);
  clear_task_flag(caller, TF_RESTORE_SIGMASK);
  UNLOCK_TASK_STRUCT(caller);

  LOCK_TASK_SIGNALS(caller);
  if (restore_sigmask)
    caller->siginfo.blocked = caller->saved_sigmask;
  else
    caller->siginfo.blocked = sa_mask;
  __update_pending_signals(caller);
  UNLOCK_TASK_SIGNALS(caller);

  /* Restore GPRs. */
  if( copy_from_user((void *)kctx,&uctx->gpr_regs,sizeof(struct gpregs) ) ) {
    goto bad_ctx;
  }
  if (copy_from_user((void *)xmm_ctx_addr, &uctx->xmm_ctx, XMM_CTX_SIZE)) {
    goto bad_ctx;
  }

  /*
   * Fixme [dg]: the ugly hack set the mxcsr content such wich will
   *             not cause an exception, redo this.
   */
  xmm_reg = (uint32_t*)xmm_ctx_addr;
  xmm_reg[6] = 0x1f80;  /* see AMD64 Architecture Programmer's Manual, vol.1, p.118 */

  fxrstor(xmm_ctx_addr);
  /* Restore retcode. */
  if( copy_from_user(&retcode,&uctx->retcode,sizeof(retcode)) ) {
    goto bad_ctx;
  }

  /* Finally, restore the address to continue execution from. */
  if( copy_from_user(&retaddr,&uctx->retaddr,sizeof(retaddr)) ) {
    goto bad_ctx;
  }

  if( !valid_user_address(retaddr) ) {
    goto bad_ctx;
  }

  uctx++;
  sframe->rip=retaddr;
  sframe->rsp=(uintptr_t)uctx;

  /* Update pending signals to let any unblocked signals be processed. */
  return retcode;

bad_ctx:
  panic("[!!!] BAD context for 'sys_sigreturn()' for task %d!\n",
        caller->tid);
  return -1;
}
