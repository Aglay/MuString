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
 * (c) Copyright 2008 Tirra <tirra.newly@gmail.com>
 * (c) Copyright 2008 Michael Tsymbalyuk <mtzaurus@gmail.com>
 *
 * mstring/amd64/sched/context.S: architecture specific context_[save|restore]
 * 			      functions
 *
 */

.text

.global arch_context_save
.global arch_context_restore
.global arch_hw_activate_task
.global arch_hw_activate_task1

#include <arch/context.h>
#include <arch/current.h>

/* assembler macros for save and restore context */
.macro CONTEXT_SAVE_ARCH_CORE  xc, pc
  movq \pc, OFFSET_PC(\xc)
  movq %rsp, OFFSET_SP(\xc)
  movq %rbx, OFFSET_RBX(\xc)
  movq %rbp, OFFSET_RBP(\xc)
  movq %r12, OFFSET_R12(\xc)
  movq %r13, OFFSET_R13(\xc)
  movq %r14, OFFSET_R14(\xc)
  movq %r15, OFFSET_R15(\xc)
.endm

.macro CONTEXT_RESTORE_ARCH_CORE  xc, pc
  movq OFFSET_R15(\xc), %r15
  movq OFFSET_R14(\xc), %r14
  movq OFFSET_R13(\xc), %r13
  movq OFFSET_R12(\xc), %r12
  movq OFFSET_RBP(\xc), %rbp
  movq OFFSET_RBX(\xc), %rbx
  movq OFFSET_SP(\xc), %rsp
  movq OFFSET_PC(\xc), \pc
.endm

arch_context_save:
	movq (%rsp), %rdx

	CONTEXT_SAVE_ARCH_CORE %rdi %rdx
	xorq %rax, %rax
	incq %rax

	ret

arch_context_restore: 
	CONTEXT_RESTORE_ARCH_CORE %rdi %rdx

	movq %rdx,(%rsp)
	xorq %rax,%rax

	ret

/* 
 * %rdi: pointer to the context of new task. 
 * %rsi: pointer to new task itself.
 * %rdx: pointer to the context of current task.
 * %rcx: top of kernel stack of new task.
 */
arch_hw_activate_task:
  pushq %rax
  SAVE_ALL

  cli
  pushq $__return_from_sleep
  /* Update this CPU's 'current' and its kernel stack. */
  movq %rsi, %gs:(CPU_SCHED_STAT_CURRENT_OFFT)
  movq %rcx, %gs:(CPU_SCHED_STAT_KSTACK_OFFT)

  /* First, save segment registers */
  mov %gs:(CPU_SCHED_STAT_USER_FS_OFFT), %rax
  mov %rax,ARCH_CTX_FS_OFFSET(%rdx)
  mov %gs:(CPU_SCHED_STAT_USER_ES_OFFT), %rax
  mov %rax,ARCH_CTX_ES_OFFSET(%rdx)
  mov %gs:(CPU_SCHED_STAT_USER_GS_OFFT), %rax
  mov %rax,ARCH_CTX_GS_OFFSET(%rdx)
  mov %gs:(CPU_SCHED_STAT_USER_DS_OFFT), %rax
  mov %rax,ARCH_CTX_DS_OFFSET(%rdx)
  mov %gs:(CPU_SCHED_STAT_USTACK_OFFT), %rax
  mov %rax,ARCH_CTX_URSP_OFFSET(%rdx)
  mov %gs:(CPU_SCHED_STAT_USER_PTD_OFFT), %rax
  mov %rax,ARCH_CTX_PTD_OFFSET(%rdx)

  /* Save %rsp */
  mov %rsp,ARCH_CTX_RSP_OFFSET(%rdx)

  /* Determine if we need to reload %cr3 */
  movq %cr3, %rax;
  cmpq %rax,ARCH_CTX_CR3_OFFSET(%rdi)
  jz leave_cr3

  movq ARCH_CTX_CR3_OFFSET(%rdi),%rax
  movq %rax, %cr3

leave_cr3:

  /* Now we can restore segment registers */
  mov ARCH_CTX_FS_OFFSET(%rdi),%rax
  mov %rax, %gs:(CPU_SCHED_STAT_USER_FS_OFFT)
  mov ARCH_CTX_ES_OFFSET(%rdi),%rax
  mov %rax, %gs:(CPU_SCHED_STAT_USER_ES_OFFT)
  mov ARCH_CTX_GS_OFFSET(%rdi),%rax
  mov %rax, %gs:(CPU_SCHED_STAT_USER_GS_OFFT)
  mov ARCH_CTX_DS_OFFSET(%rdi),%rax
  mov %rax, %gs:(CPU_SCHED_STAT_USER_DS_OFFT)
  mov ARCH_CTX_URSP_OFFSET(%rdi),%rax
  mov %rax,%gs:(CPU_SCHED_STAT_USTACK_OFFT)
  mov ARCH_CTX_PTD_OFFSET(%rdi),%rax
  mov %rax,%gs:(CPU_SCHED_STAT_USER_PTD_OFFT)

  /* Setup userspace works status. */
  mov %rdi,%rax
  add $ARCH_CTX_UWORKS_OFFSET,%rax
  mov %rax,%gs:(CPU_SCHED_STAT_USER_WORKS_OFFT)

  /* Now we can reload %rsp and return to perform actual context switch. */
  movq ARCH_CTX_RSP_OFFSET(%rdi), %rsp
  retq

__return_from_sleep:
  RESTORE_ALL
  popq %rax
  retq
