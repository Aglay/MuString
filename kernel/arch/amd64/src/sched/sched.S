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
 */

#include <arch/context.h>

.code64
.text

.globl kthread_fork_path
kthread_fork_path:
    RESTORE_ALL
    popq %rax
    iretq

.globl user_fork_path_test
user_fork_path_test:
    xorq %rax,%rax

.globl user_fork_path
user_fork_path:
    /* Before returning from fork we should check for pending works.
     * Note that %rax contains return code.
     */
    sti
    mov %gs:CPU_SCHED_STAT_USER_WORKS_OFFT, %rbx

    cmpq $0, (%rbx)
    je no_works_after_fork
    pushq %rax
    movq $__SYCALL_UWORK, %rdi
    movq %rax, %rsi
    movq %rsp, %rdx
    addq $8, %rdx
    callq handle_uworks
    popq %rax
no_works_after_fork:
    xorq %rax,%rax
resume_userspace:
    RESTORE_ALL    
    RESTORE_USER_SEGMENT_REGISTERS
    cli
    movl %gs:(CPU_SCHED_STAT_USER_GS_OFFT),%eax
    swapgs
    movl %eax,%gs
    popq %rax
    orq $(1 << 9),HW_INTERRUPT_CTX_RFLAGS_OFFT(%rsp)
    iretq

.globl arch_cpu_relax
arch_cpu_relax:
    hlt
