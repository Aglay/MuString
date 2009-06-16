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
 * /mstring/amd64/syscalls.S: low level syscall-related, AMD64-specific
 *                        functionality
 */

#include <arch/context.h>
#include <arch/seg.h>
#include <mstring/errno.h>

.global syscall_point

/* NOTE: 'syscall_point' must immediately follow the list
 * of syscalls since it is used for determening the amount
 * of available system calls.
 * NOTE: since %rcx is used by the 'syscall' opcode for saving
 * return address, processes that invoke 'syscall' must pass
 * the fourth parameter via %r10.
 */
syscall_table:
	.quad 	sys_create_task             /* 0 */
	.quad 	sys_task_control
	.quad   sys_mmap
	.quad   sys_create_port
	.quad   sys_port_send           	
	.quad   sys_port_receive            /* 5 */
	.quad   sys_port_reply
	.quad   sys_allocate_ioports
	.quad	sys_free_ioports
	.quad   sys_create_irq_counter_array
	.quad   sys_wait_on_irq_array           /* 10 */
        .quad   sys_ipc_port_poll
	.quad   sys_nanosleep
	.quad   sys_scheduler_control
	.quad   sys_exit
	.quad   sys_open_channel                /* 15 */
	.quad   sys_close_channel
	.quad   sys_close_port
	.quad   sys_control_channel
	.quad   sys_port_send_iov
	.quad   sys_sync_create_object          /* 20  */
	.quad   sys_sync_control
	.quad   sys_sync_destroy
	.quad   sys_kill
	.quad   sys_signal
	.quad   sys_sigreturn                   /* 25 */
	.quad   sys_port_send_iov_v
	.quad   sys_port_reply_iov
	.quad   sys_sigaction
	.quad   sys_thread_kill
	.quad   sys_sigprocmask                 /* 30 */
	.quad   sys_thread_exit
	.quad   sys_timer_create
	.quad   sys_timer_control
	.quad   sys_munmap
	.quad   sys_thread_wait                 /* 35 */
	.quad   sys_port_msg_read
	.quad   sys_kernel_control
	.quad   sys_timer_delete
	.quad   sys_sigwaitinfo
	.quad   sys_sched_yield                 /* 40 */
	.quad   sys_memobj_create
	.quad   sys_fork
	.quad   sys_grant_pages
	.quad   sys_waitpid
        .quad   sys_alloc_dma_pages             /* 45  */
        .quad   sys_free_dma_pages

/* Don't change the order of these macros. */
#define NUM_OF_SYSCALLS \
	(syscall_point-syscall_table)/8

syscall_point:
	/* Local interrupts are disabled upon entering the entrypoint.
	 */
	swapgs
	movq %rsp,%gs:CPU_SCHED_STAT_USTACK_OFFT
	movq %gs:CPU_SCHED_STAT_KSTACK_OFFT,%rsp
	SAVE_AND_LOAD_SEGMENT_REGISTERS

	/* Since we use IRET instead of SYSRETURN to return from syscall,
	 *  we need to setup a fake interrupt stack frame on the stack.
	 */
	subq $HW_INTERRUPT_CTX_SIZE,%rsp
	movq %rcx,HW_INTERRUPT_CTX_RIP_OFFT(%rsp)
	movq $(GDT_SEL(UCODE_DESCR) | SEG_DPL_USER),HW_INTERRUPT_CTX_CS_OFFT(%rsp)

	movq $(GDT_SEL(UDATA_DESCR) | SEG_DPL_USER),HW_INTERRUPT_CTX_SS_OFFT(%rsp)
	movq %gs:CPU_SCHED_STAT_USTACK_OFFT,%rcx
	movq %rcx,HW_INTERRUPT_CTX_RSP_OFFT(%rsp)
	subq $8,%rsp /* Simulate 'push %rax' */

	/* After changing stack pointer, we can enable IRQs */
	sti
	SAVE_ALL

	cmp $NUM_OF_SYSCALLS,%rax
	jb good_syscall_number

	movq $-ENOSYS,%rax
	jmp return_from_syscall

good_syscall_number:
	/* Since %rcx contains the return address, we should reload
	 * %rcx from %r10.
	 */
	movq %r10,%rcx

	call *syscall_table(,%rax,8)
	jmp return_from_syscall