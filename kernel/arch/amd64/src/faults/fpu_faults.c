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
 * mstring/amd64/faults/fpu_faults.c: contains routines for dealing with
 *  FPU-related x86_64 CPU fauls.
 *
 */

#include <arch/types.h>
#include <arch/page.h>
#include <arch/fault.h>
#include <arch/interrupt.h>
#include <mstring/panic.h>
#include <mstring/kprintf.h>
#include <arch/mem.h>
#include <mstring/smp.h>

void divide_by_zero_fault_handler_impl(interrupt_stack_frame_t *stack_frame)
{
    kprintf( "  [!!] #DE exception raised !, RIP = %p\n", stack_frame->rip );
    interrupts_disable();
    for(;;);
}

void overflow_fault_handler_impl(void)
{
    kprintf( "  [!!] #Overflow exception raised !");
    interrupts_disable();
    for(;;);
}

void coprocessor_segment_overrun_fault_handler_impl(interrupt_stack_frame_t *stack_frame)
{
    kprintf( "  [!!] #FPU segment overrun exception raised ! RIP = %p\n", stack_frame->rip );
    interrupts_disable();
    for(;;);
}

void fpu_fault_handler_impl(interrupt_stack_frame_t *stack_frame)
{
    kprintf( "  [!!] #FPU exception raised ! RIP = %p\n", stack_frame->rip );
    interrupts_disable();
    for(;;);
}

void simd_fault_handler_impl(interrupt_stack_frame_t *stack_frame)
{
    kprintf( "  [!!] #SIMD exception raised. RIP = %p!\n", stack_frame->rip );
    interrupts_disable();
    for(;;);
}

