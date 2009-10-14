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
 * (c) Copyright 2009 Dan Kruchinin <dk@jarios.org>
 *
 */

#include <config.h>
#include <arch/seg.h>
#include <arch/fault.h>
#include <arch/context.h>
#include <arch/cpu.h>
#include <mm/vmm.h>
#include <mstring/kprintf.h>
#include <mstring/task.h>
#include <mstring/types.h>


#if 0
#define get_fault_address(x) \
    __asm__ __volatile__( "movq %%cr2, %0" : "=r"(x) )

static bool __read_user_safe(uintptr_t addr,uintptr_t *val)
{
  uintptr_t *p;
  page_idx_t pidx = vaddr_to_pidx(task_get_rpd(current_task()), addr);

  if( pidx == PAGE_IDX_INVAL ) {
    return false;
  }

  p=(uintptr_t*)(pframe_id_to_virt(pidx)+(addr & PAGE_MASK));
  *val=*p;
  return true;
}

void __dump_stack(uintptr_t ustack,vmm_t *vmm)
{
  int i,show;
  uintptr_t d;
  vmrange_t *vmr=NULL;

  kprintf_fault("\nTop %d words of userspace stack (RSP=%p).\n\n",
          CONFIG_NUM_STACKWORDS,ustack);

#ifdef CONFIG_DUMP_USER_CALL_PATH
  if( vmm ) {
    vmr=vmrange_find(vmm,USPACE_VADDR_BOTTOM,USPACE_VADDR_BOTTOM+PAGE_SIZE,NULL);
    kprintf_fault("VMR: %p\n",vmr);
  }
#endif

  for(i=0;i<CONFIG_NUM_STACKWORDS;i++) {
    if( __read_user_safe(ustack,&d) ) {
      if( vmr ) {
        show=(d >= vmr->bounds.space_start && d < vmr->bounds.space_end);
      } else {
        show=1;
      }

      if( show ) {
        kprintf_fault("  <%p>\n",d);
      }
    } else {
      kprintf_fault("  <Invalid stack pointer>\n");
    }
    ustack += sizeof(uintptr_t);
  }
}

void invalid_tss_fault_handler_impl(interrupt_stack_frame_err_t *stack_frame)
{
    kprintf_fault( "  [!!] #Invalid TSS exception raised !\n" );
}

void stack_fault_handler_impl(interrupt_stack_frame_err_t *stack_frame)
{
    kprintf_fault( "  [!!] #Stack exception raised !\n" );
}

void segment_not_present_fault_handler_impl(interrupt_stack_frame_err_t *stack_frame)
{
    kprintf_fault( "  [!!] #Segment not present exception raised !\n" );
}

void general_protection_fault_handler_impl(interrupt_stack_frame_err_t *stack_frame)
{
  regs_t *regs=(regs_t *)(((uintptr_t)stack_frame)-sizeof(struct __gpr_regs)-8);

  if (!default_console()->is_enabled)
    default_console()->enable();

  if( kernel_fault(stack_frame) ) {
    goto kernel_fault;
  }

  kprintf_fault("[CPU %d] Unhandled user-mode GPF exception! Stopping CPU with error code=%d.\n\n",
          cpu_id(), stack_frame->error_code);
  goto stop_cpu;

kernel_fault:
  kprintf_fault("[CPU %d] (rip = %p) Unhandled kernel-mode GPF exception! Stopping CPU with error code=%d.\n\n",
                cpu_id(), stack_frame->rip, stack_frame->error_code);
stop_cpu:  
  fault_dump_regs(regs,stack_frame->rip);
  show_stack_trace(stack_frame->old_rsp);
#ifdef CONFIG_DUMP_USTACK
  if (!kernel_fault(stack_frame))
    __dump_user_stack(stack_frame->old_rsp);
#endif
  interrupts_disable();
  for(;;);
}

void page_fault_fault_handler_impl(interrupt_stack_frame_err_t *stack_frame)
{
  uint64_t invalid_address,fixup;
  regs_t *regs=(regs_t *)(((uintptr_t)stack_frame)-sizeof(struct __gpr_regs)-8);
  usiginfo_t siginfo;
  task_t *faulter=current_task();
  vmm_t *vmm = NULL;

  get_fault_address(invalid_address);
  fixup = fixup_fault_address(stack_frame->rip);
  if(PFAULT_SVISOR(stack_frame->error_code) && !fixup) {
    goto kernel_fault;      
  }
  else {
    /*
     * PF in user-space or "fixuped" fault in kernel.
     * Try to find out correspondig VM range and handle the faut using range's memory object.
     */

    vmm = current_task()->task_mm;
    uint32_t errmask = 0;
    int ret = -EFAULT;

    if (!PFAULT_READ(stack_frame->error_code))
      errmask |= PFLT_WRITE;
    if (PFAULT_NXE(stack_frame->error_code))
      errmask |= PFLT_NOEXEC;
    if (PFAULT_PROTECT(stack_frame->error_code))
      errmask |= PFLT_PROTECT;
    else
      errmask |= PFLT_NOT_PRESENT;

    ret = vmm_handle_page_fault(vmm, invalid_address, errmask);
    if (!ret) {
      return;
    }

    kprintf_fault("[DEBUG] Failed to handle page fault of "
                  "task %d [ERR = %d]\n", faulter->pid, ret);

    if (fixup != 0) {
      goto kernel_fault;
    }
  }

#ifdef CONFIG_SEND_SIGSEGV_ON_FAULTS
  goto send_sigsegv;
#endif

  kprintf_fault("[CPU %d] Unhandled user-mode PF exception! Stopping CPU with error code=%d.\n\n",
                cpu_id(), stack_frame->error_code);
  goto stop_cpu;

kernel_fault:
  /* First, try to fix this exception. */
  if( fixup != 0 ) {
    stack_frame->rip=fixup;
    return;
  }

  kprintf_fault("[CPU %d] Unhandled kernel-mode PF exception! Stopping CPU with error code=%d.\n\n",
          cpu_id(), stack_frame->error_code);  
stop_cpu:
  fault_dump_regs(regs,stack_frame->rip);
  kprintf_fault( " Invalid address: %p\n", invalid_address );
  kprintf_fault( " RSP: %p\n", stack_frame->old_rsp);

  if( kernel_fault(stack_frame) ) {
    show_stack_trace(stack_frame->old_rsp);
  }
#ifdef CONFIG_DUMP_USPACE_STACK
  else {
    __dump_stack(stack_frame->old_rsp,vmm);
  }
#endif /* CONFIG_DUMP_USTACK */

  interrupts_disable();
  for(;;);

#ifdef CONFIG_SEND_SIGSEGV_ON_FAULTS
send_sigsegv:
#endif /* CONFIG_SEND_SIGSEGV_ON_FAULTS */
  fault_dump_regs(regs,stack_frame->rip);                                                               
  kprintf_fault( " Invalid address: %p\n", invalid_address );                                           
  kprintf_fault( " RSP: %p\n", stack_frame->old_rsp);

  /* Send user the SIGSEGV signal. */
  INIT_USIGINFO_CURR(&siginfo);
  siginfo.si_signo=SIGSEGV;
  siginfo.si_code=SEGV_MAPERR;
  siginfo.si_addr=(void *)invalid_address;

  kprintf_fault("[!!] Sending SIGSEGV to %d:%d ('%s')\n",
                faulter->pid,faulter->tid,faulter->short_name);
  send_task_siginfo(faulter,&siginfo,true,NULL);
}
#endif

static inline void display_unhandled_pf_info(struct fault_ctx *fctx, uintptr_t fault_addr)
{
  fault_describe("PAGE FAULT", fctx);
  kprintf_fault("  Invalid address: %p, RIP: %p\n",
                fault_addr, fctx->istack_frame->rip);
  fault_dump_info(fctx);
}

#ifdef CONFIG_SEND_SIGSEGV_ON_FAULTS
static void send_sigsegv(uintptr_t fault_addr)
{  
  usiginfo_t siginfo;

  /* Send user the SIGSEGV signal. */
  INIT_USIGINFO_CURR(&siginfo);
  siginfo.si_signo=SIGSEGV;
  siginfo.si_code=SEGV_MAPERR;
  siginfo.si_addr=(void *)fault_addr;

  kprintf_fault("[!!] Sending SIGSEGV to %d:%d ('%s')\n",
                faulter->pid,faulter->tid,faulter->short_name);
  send_task_siginfo(faulter,&siginfo,true,NULL);
}
#endif /* CONFIG_SEND_SIGSEGV_ON_FAULTS */

struct fixup_record {
  uint64_t fault_address, fixup_address;
}; 

extern int __ex_table_start, __ex_table_end;

static uint64_t fixup_fault_address(uint64_t fault_address)
{
  struct fixup_record *fr = (struct fixup_record *)&__ex_table_start;
  struct fixup_record *end = (struct fixup_record *)&__ex_table_end;

  while (fr<end) {
    if (fr->fault_address == fault_address) {
      return fr->fixup_address;
    }

    fr++;
  }

  return 0;
}

#define PFAULT_NP(errcode)      (((errcode) & 0x1) == 0)
#define PFAULT_PROTECT(errcode) ((errcode) & 0x01)
#define PFAULT_READ(errcode)    (((errcode) & 0x02) == 0)
#define PFAULT_WRITE(errcode)   ((errcode) & 0x02)
#define PFAULT_SVISOR(errcode)  (((errcode) & 0x04) == 0)
#define PFAULT_USER(errcode)    ((errcode) & 0x04)
#define PFAULT_NXE(errcode)     ((errcode) & 0x10)

void FH_page_fault(struct fault_ctx *fctx)
{
  uintptr_t fault_addr;
  struct intr_stack_frame *stack_frame = fctx->istack_frame;

  fault_addr = read_cr2();
  if (IS_KERNEL_FAULT(fctx)) {
    uintptr_t fixup_addr;

    fixup_addr = fixup_fault_address(stack_frame->rip);
    if (!fixup_addr) {      
      display_unhandled_pf_info(fctx, fault_addr);
      kprintf_fault("HERE??\n");
      goto stop_cpu;
    }

    stack_frame->rip = fixup_addr;
    return;
  }
  else {
    kprintf_fault("uspacefault\n");
    vmm_t *vmm = current_task()->task_mm;
    uint32_t errmask = 0;
    int ret = -EFAULT;

    if (!PFAULT_READ(fctx->errcode))
      errmask |= PFLT_WRITE;
    if (PFAULT_NXE(fctx->errcode))
      errmask |= PFLT_NOEXEC;
    if (PFAULT_PROTECT(fctx->errcode))
      errmask |= PFLT_PROTECT;
    else
      errmask |= PFLT_NOT_PRESENT;

    ret = vmm_handle_page_fault(vmm, fault_addr, errmask);
    if (!ret) {
      kprintf_fault("uspacefault: done\n");
      return;
    }
    
    display_unhandled_pf_info(fctx, fault_addr);
#ifdef CONFIG_SEND_SIGSEGV_ON_FAULTS
    send_sigsegv(fault_addr);
    return;
#endif /* CONFIG_SEND_SIGSEGV_ON_FAULTS */
  }

stop_cpu:
  __stop_cpu();
  kprintf_fault("CPU IS STOPPED\n");
}

