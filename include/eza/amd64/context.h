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
 * include/eza/amd64/context.h: structure definion for context and related stuff
 *                              assembler macros and some constants
 *
 */

#ifndef __ARCH_CONTEXT_H__
#define __ARCH_CONTEXT_H__ /* there are several context.h(es) */

#define OFFSET_SP   0x0
#define OFFSET_PC   0x8
#define OFFSET_RBX  0x10
#define OFFSET_RBP  0x18
#define OFFSET_R12  0x20
#define OFFSET_R13  0x28
#define OFFSET_R14  0x30
#define OFFSET_R15  0x38
#define OFFSET_IPL  0x40

#define OFFSET_TLS  OFFSET_IPL

/* Save all general purpose registers  */
#define SAVE_GPR \
    pushq %r8; \
    pushq %r9; \
    pushq %r10; \
    pushq %r11; \
    pushq %r12; \
    pushq %r13; \
    pushq %r14; \
    pushq %r15; \
    pushq %rbx; \
    pushq %rcx; \
    pushq %rdx; \
    pushq %rdi; \
    pushq %rsi; \
    pushq %rbp; \

#define RESTORE_GPR \
    popq %rbp; \
    popq %rsi; \
    popq %rdi; \
    popq %rdx; \
    popq %rcx; \
    popq %rbx; \
    popq %r15; \
    popq %r14; \
    popq %r13; \
    popq %r12; \
    popq %r11; \
    popq %r10; \
    popq %r9; \
    popq %r8; \

#define NUM_GPR_SAVED 14
#define SAVED_GPR_SIZE (NUM_GPR_SAVED * 8)

/* We assume that all GRPs were saved earlier !
 * Allocate an area for saving MMX & FX state. Finally, we must adjust %rdi
 * to point just after saved GPRs area.
 */
#define SAVE_MM \
  mov %rsp, %r8; \
  mov %rsp, %r10; \
  and $0xfffffffffffffe00, %r8; \
  mov %rsp, %r9; \
  sub %r8, %r9; \
  add $512, %r9; \
  sub %r9, %rsp; \
  fxsave (%rsp); \
  pushq %r9; \

#define RESTORE_MM \
  popq %r10; \
  fxrstor (%rsp); \
  add %r10, %rsp;

/* NOTE: SAVE_MM initializes %rsi so that it points to iterrupt/exception stack frame. */
#define SAVE_ALL \
  SAVE_GPR \
  cli; \
  SAVE_MM \


#define RESTORE_ALL \
  RESTORE_MM \
  RESTORE_GPR

#define SAVED_REGISTERS_SIZE \
   ((NUM_GPR_SAVED)*8)

/* Offsets to parts of CPU exception stack frames. */
#define INT_STACK_FRAME_CS_OFFT 8

/* assembler macros for save and restore context */
#ifdef __ASM__

.macro CONTEXT_SAVE_ARCH_CORE  xc:req pc:req
  movq \pc, OFFSET_PC(\xc)
  movq %rsp, OFFSET_SP(\xc)
  movq %rbx, OFFSET_RBX(\xc)
  movq %rbp, OFFSET_RBP(\xc)
  movq %r12, OFFSET_R12(\xc)
  movq %r13, OFFSET_R13(\xc)
  movq %r14, OFFSET_R14(\xc)
  movq %r15, OFFSET_R15(\xc)
.endm

.macro CONTEXT_RESTORE_ARCH_CORE  xc:req pc:req
  movq OFFSET_R15(\xc), %r15
  movq OFFSET_R14(\xc), %r14
  movq OFFSET_R13(\xc), %r13
  movq OFFSET_R12(\xc), %r12
  movq OFFSET_RBP(\xc), %rbp
  movq OFFSET_RBX(\xc), %rbx
  movq OFFSET_SP(\xc), %rsp
  movq OFFSET_PC(\xc), \pc
.endm

#endif /* __ASM__ */

/* amd64 specific note: ABI describes that stack
 * must be aligned to 16 byte , this can affect va_arg and so on ...
 */
#define SP_DELTA  16

/* Kernel-space task context related stuff. */
#define ARCH_CTX_CR3_OFFSET  0x0
#define ARCH_CTX_RSP_OFFSET  0x8
#define ARCH_CTX_FS_OFFSET   0x10
#define ARCH_CTX_GS_OFFSET   0x18
#define ARCH_CTX_ES_OFFSET   0x20
#define ARCH_CTX_DS_OFFSET   0x28

#ifdef __ASM__

/* extra bytes on the stack after CPU exception stack frame: %rax */
#define INT_STACK_EXTRA_PUSHES  8

#include <eza/arch/current.h>
#include <eza/arch/page.h>

#define ENTER_INTERRUPT_CTX(label,extra_pushes) \
	cmp $KERNEL_SELECTOR(KTEXT_DES),extra_pushes+INT_STACK_FRAME_CS_OFFT(%rsp) ;\
	je label; \
        swapgs ;  \
        mov %ds, %gs:CPU_SCHED_STAT_USER_DS_OFFT;       \
        mov %es, %gs:CPU_SCHED_STAT_USER_ES_OFFT;       \
        mov %fs, %gs:CPU_SCHED_STAT_USER_FS_OFFT;       \
        mov %gs:(CPU_SCHED_STAT_KERN_DS_OFFT),%ds;      \
label:	;\
	incq %gs:CPU_SCHED_STAT_IRQCNT_OFFT ;\
	SAVE_ALL ;\
	sti

#define COMMON_INTERRUPT_EXIT_PATH \
         jmp return_from_common_interrupt;
     
#endif

#ifndef __ASM__

#include <eza/arch/types.h>

typedef struct __context_t { /* others don't interesting... */
  uintptr_t sp;
  uintptr_t pc;

  uint64_t rbx;
  uint64_t rbp;
  uint64_t r12;
  uint64_t r13;
  uint64_t r14;
  uint64_t r15;

  ipl_t ipl;
} __attribute__ ((packed)) context_t;

typedef struct __arch_context_t {
    uintptr_t cr3, rsp, fs, gs, es, ds;
} arch_context_t;

/* Structure that represents GPRs on the stack upon entering
 * kernel mode during a system call.
 */
typedef struct __regs {
  /* Kernel-saved registers. */
  uint64_t rbp, rsi, rdi, rdx, rcx, rbx;
  uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
  uint64_t rax;
    
  /* CPU-saved registers. */
  uint64_t rip, cs, rflags, old_rsp, old_ss;
} regs_t;

#endif /* __ASM__ */

#endif /* __ARCH_CONTEXT_H__ */

