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
 *
 * eza/amd64/platform.c: specific platform initialization.
 *
 */

#include <config.h>
#include <ds/iterator.h>
#include <eza/arch/types.h>
#include <eza/arch/apic.h>
#include <eza/arch/ioapic.h>
#include <eza/interrupt.h>
#include <eza/timer.h>
#include <mm/mm.h>
#include <mm/page.h>
#include <mm/pfalloc.h>
#include <mm/pfalloc.h>
#include <eza/arch/page.h>
#include <eza/arch/smp.h>
#include <mlibc/kprintf.h>
#include <mlibc/unistd.h>
#include <eza/swks.h>
#include <eza/actbl.h>
#include <config.h>

extern volatile uint32_t local_apic_base;
extern volatile uint8_t local_apic_ids[NR_CPUS];

#ifdef CONFIG_APIC

void arch_specific_init(void)
{
  int r, i;
	uint32_t apic_base = 0;
	int n;
	uint8_t id;

  kprintf("[HW] Init arch specific ...\n");
	
	r = get_acpi_lapic_info(&apic_base, local_apic_ids, NR_CPUS, &n);

  local_apic_bsp_switch(); 
  if (local_bsp_apic_init() == -1)
		panic("[HW] APIC is not found!\n");

#ifdef CONFIG_SMP
	if (r > 0) {
		/* debug */
		kprintf("APIC ids: ");
		for (i = 0; i < r; i++)
			kprintf("%d \n", local_apic_ids[i]);

		kprintf("\n");
		/* check that apic physical id order is correct */
		id = get_local_apic_id();
		for (i = 0; (i < r) && (local_apic_ids[i] != id); i++);
		
		if (i == r) {
			kprintf("Local apic information is invalid!\n");
			return;
		} else if (i) {
			local_apic_ids[i] = local_apic_ids[0];
			local_apic_ids[0] = id;
		}
		
		kprintf("Launch other cpus ... ");
		arch_smp_init(r);
		kprintf("OK\n");
		if (r < n)
			kprintf("%d CPUs is found, %d is launched\n", n, r);
	}
#endif
}

#else

void arch_specific_init(void)
{
	/* dummy */
}

#endif /* CONFIG_APIC */

#ifdef CONFIG_SMP

void arch_ap_specific_init(void)
{
   local_ap_apic_init();
}

#endif

void arch_initialize_swks(void)
{
 /* According to the x86 programming manual, we must reserve
  * eight bits after the last available I/O port in all IOPMs.
  */
  swks.ioports_available=(PAGE_SIZE-sizeof(tss_t)-1)*8;
}
