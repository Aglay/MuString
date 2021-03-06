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
 * (c) Copyright 2008 MadTirra <madtirra@jarios.org>
 *
 * mstring/amd64/timer.c: arch specific timers init
 *                          
 *
 */

#include <config.h>
#include <arch/i8254.h>
#include <arch/apic.h>
#include <arch/cpufeatures.h>
#include <mstring/timer.h>
#include <mstring/time.h>
#include <mstring/kprintf.h>
#include <mstring/types.h>

extern void i8254_resume(void);
extern void i8254_suspend(void);

void arch_timer_init(void)
{
  i8254_init();
  if (!cpu_has_feature(X86_FTR_APIC)) {
    kprintf(KO_WARNING "Your CPU doesn't support APIC\n");
    for (;;);
  }
  else {    
    lapic_timer_init(0);
  }
}
