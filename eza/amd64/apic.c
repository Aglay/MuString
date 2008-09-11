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
 * eza/amd64/apic.c: implements local APIC support driver.
 *
 */

#include <config.h>
#include <eza/interrupt.h>
#include <eza/arch/8259.h>
#include <eza/arch/i8254.h>
#include <eza/arch/types.h>
#include <eza/arch/asm.h>
#include <eza/arch/apic.h>
#include <eza/arch/ioapic.h>
#include <eza/arch/mm_types.h>
#include <eza/arch/interrupt.h>
#include <eza/arch/gdt.h>
#include <mlibc/kprintf.h>
#include <mlibc/unistd.h>
#include <mlibc/string.h>

/*
 * Black mages from intel and amd wrote that
 * local APIC is memory mapped, I'm afraid on this 
 * solution looks ugly ...
 * TODO: I get unclear sense while some higher 
 * abstraction not being implemented.
 */

volatile static struct __local_apic_t *local_apic=(struct __local_apic_t *)APIC_BASE;

/* 
 * default functions to access APIC (local APIC)
 * I think that gcc can try to make optimization on it
 * to avoid I'm stay `volatile` flag here.
 */
static inline uint32_t __apic_read(ulong_t rv)
{
  return *((volatile uint32_t *)(APIC_BASE+rv));
}

static inline void __apic_write(ulong_t rv,uint32_t val)
{
  *((volatile uint32_t *)(APIC_BASE+rv))=val;
}

static uint32_t __get_maxlvt(void)
{
  apic_version_t version=local_apic->version;

  return version.max_lvt;
}

static void __set_lvt_lint_vector(uint32_t lint_num,uint32_t vector)
{
  apic_lvt_lint_t lvt_lint;

  if(!lint_num) {
    lvt_lint=local_apic->lvt_lint0;
    lvt_lint.vector=vector;
    local_apic->lvt_lint0.reg=lvt_lint.reg;
  }  else {
    lvt_lint=local_apic->lvt_lint1;
    lvt_lint.vector=vector;
    local_apic->lvt_lint1.reg=lvt_lint.reg;
  }
}

static void __enable_apic(void)
{
  apic_svr_t svr=local_apic->svr;

  svr.apic_enabled=0x1;
  svr.cpu_focus=0x1;
  local_apic->svr.reg=svr.reg;
}

static void __disable_apic(void)
{
  apic_svr_t svr=local_apic->svr;

  svr.apic_enabled=0x0;
  svr.cpu_focus=0x0;
  local_apic->svr.reg=svr.reg;
}

void __local_apic_clear(void)
{
  uint32_t max_lvt;
  uint32_t v;
  apic_lvt_error_t lvt_error=local_apic->lvt_error;
  apic_lvt_timer_t lvt_timer=local_apic->lvt_timer;
  apic_lvt_lint_t lvt_lint=local_apic->lvt_lint0;
  apic_lvt_pc_t lvt_pc=local_apic->lvt_pc;

  max_lvt=__get_maxlvt();

  if(max_lvt>=3) {
    v=0xfe;
    lvt_error.vector=v;
    lvt_error.mask |= (1 << 0);
    local_apic->lvt_error.reg=lvt_error.reg;
  }

  /* mask timer and LVTs*/
  lvt_timer.mask = 0x1;
  local_apic->lvt_timer.reg=lvt_timer.reg;
  lvt_lint.mask=0x1;
  local_apic->lvt_lint0.reg = lvt_lint.reg;
  lvt_lint=local_apic->lvt_lint1;
  lvt_lint.mask=0x1;
  local_apic->lvt_lint1.reg = lvt_lint.reg;

  if(max_lvt>=4) {
    lvt_pc.mask = 0x1;
    local_apic->lvt_pc.reg=lvt_pc.reg;
  }
  
}

uint32_t get_apic_version(void)
{
  apic_version_t version=local_apic->version;

  return version.version;
}

static int __local_apic_check(void)
{
  uint32_t v0;

  /* version check */
  v0=get_apic_version();
  v0=(v0)&0xffu;
  if(v0==0x0 || v0==0xff)
    return -1;

  /*check for lvt*/
  v0=__get_maxlvt();
  if(v0<0x02 || v0==0xff)
  return -1;

  return 0;
}

void local_apic_send_eoi(void)
{
  local_apic->eoi.eoi=APIC_INT_EOI;
}

uint32_t get_local_apic_id(void)
{
  apic_id_t version=local_apic->id;

  return version.phy_apic_id;
}

void set_apic_spurious_vector(uint32_t vector)
{
  apic_svr_t svr=local_apic->svr;
  svr.spurious_vector=vector;
  local_apic->svr.reg=svr.reg;
}

void set_apic_dfr_mode(uint32_t mode)
{
  apic_dfr_t dfr=local_apic->dfr;
  dfr.mode=mode;
  local_apic->dfr.reg=dfr.reg;
}

void set_apic_ldr_logdest(uint32_t dest)
{
  apic_ldr_t ldr=local_apic->ldr;
  ldr.log_dest=dest;
  local_apic->ldr.reg=ldr.reg;
}

/*init functions makes me happy*/
void local_bsp_apic_init(void)
{
  uint32_t v;
  int i=0,l;
  apic_lvt_lint_t lvt_lint;
  apic_icr1_t icr1;
  apic_tpr_t tpr=local_apic->tpr;

  /*test*/
  kprintf("id=%p,version=%p\n",&local_apic->id,&local_apic->version);

  kprintf("[LW] Checking APIC is present ... ");
  if(__local_apic_check()<0) {
    kprintf("FAIL\n");
    return;
  } else
  kprintf("OK\n");

  v=get_apic_version();
  kprintf("[LW] APIC version: %d\n",v);
  /* first we're need to clear APIC to avoid magical results */

    __local_apic_clear();

  /*manual recommends to accept all*/
  /*tpr.priority=0xffu;
    local_apic->tpr.reg=tpr.reg;*/

  /**/
  for(i=7;i>=0;i--){
    v=local_apic->isr[i].bits;
    for(l=31;l>=0;l--)
      if(v & (1 << l))
	local_apic_send_eoi();
  }

  /* enable APIC */
  __enable_apic();

  local_apic_timer_init();

  /*set spurois vector*/
  set_apic_spurious_vector(0xff);

  /* enable wire mode*/
  //local_apic->lvt_lint0.mask |= (1 << 0);

  /* set nil vectors */
  __set_lvt_lint_vector(0,0x32);
  __set_lvt_lint_vector(1,0x33);
  /*set mode#7 extINT for lint0*/
  lvt_lint=local_apic->lvt_lint0;
  lvt_lint.tx_mode=0x7;
  lvt_lint.mask=0x1;
  local_apic->lvt_lint0.reg=lvt_lint.reg;
  /*set mode#4 NMI for lint1*/
  lvt_lint=local_apic->lvt_lint1;
  lvt_lint.tx_mode=0x4;
  lvt_lint.mask=0x1;
  local_apic->lvt_lint1.reg=lvt_lint.reg;

  /*tx_mode & polarity set to 0 on both lintx*/
  lvt_lint=local_apic->lvt_lint0;
  lvt_lint.tx_status = 0x0;
  lvt_lint.polarity = 0x0;
  local_apic->lvt_lint0.reg=lvt_lint.reg;
  lvt_lint=local_apic->lvt_lint1;
  lvt_lint.tx_status = 0x0;
  lvt_lint.polarity = 0x0;
  local_apic->lvt_lint1.reg=lvt_lint.reg;
  /* ok, now we're need to set esr vector to 0xfe */
  local_apic->lvt_error.vector = 0xfe;

  /*enable to receive errors*/
  if(__get_maxlvt()>3)
    local_apic->esr.tx_cs_err = 0x0;

  /* set icr1 registers*/
  icr1=local_apic->icr1;
  icr1.tx_mode=TXMODE_INIT;
  icr1.rx_mode=DMODE_PHY;
  icr1.level=0x0;
  icr1.shorthand=0x2;
  icr1.trigger=0x1;
  local_apic->icr1.reg=icr1.reg;

  set_apic_ldr_logdest(0x0);
  set_apic_dfr_mode(0xf);

  /* remap irq for timer */
  //  io_apic_set_ioredir(0x00,0xff,32,0x0);
  //  io_apic_bsp_init();

  /*  for(i=0;i<16;i++)
      io_apic_set_ioredir(i,0xff,i,0x0);*/

  /*FIXME: just for debug */
  /*output info from esr*/
  /*  kprintf("[APIC] errors:\n");
  kprintf("tx_cs_err: %d, tx_accept_err: %d, rx_accept_err: %d\ntx_ill_vec: %d,rx_ill_vec: %d,reg_ill_addr: %d\n",
	  local_apic->esr.tx_cs_err,local_apic->esr.tx_accept_err,local_apic->esr.rx_accept_err,local_apic->esr.tx_illegal_vector,
	  local_apic->esr.rx_illegal_vector,local_apic->esr.reg_illegal_addr)*/;

}

void local_apic_bsp_switch(void)
{
  kprintf("[LW] Leaving PIC mode to APIC mode ... ");
  outb(0x70,0x22);
  outb(0x71,0x23);

  kprintf("OK\n");
}

/* APIC timer implementation */
void local_apic_timer_enable(void)
{
  apic_lvt_timer_t lvt_timer=local_apic->lvt_timer;

  lvt_timer.mask=0x0;
  local_apic->lvt_timer.reg=lvt_timer.reg;
}

void local_apic_timer_disable(void)
{
  apic_lvt_timer_t lvt_timer=local_apic->lvt_timer;

  lvt_timer.mask=0x1;
  local_apic->lvt_timer.reg=lvt_timer.reg;
}

static void __local_apic_timer_calibrate(uint32_t x)
{
  apic_timer_dcr_t timer_dcr=local_apic->timer_dcr;

  switch(x) {
  case 1:    timer_dcr.divisor=0xb;    break;
  case 2:    timer_dcr.divisor=0x0;    break;
  case 4:    timer_dcr.divisor=0x1;    break;
  case 8:    timer_dcr.divisor=0x2;    break;
  case 16:    timer_dcr.divisor=0x3;    break;
  case 32:    timer_dcr.divisor=0x8;    break;
  case 64:    timer_dcr.divisor=0x9;    break;
  case 128:    timer_dcr.divisor=0xa;    break;
  default:    return;
  }
  local_apic->timer_dcr.reg=timer_dcr.reg;
}

void local_apic_timer_calibrate(uint32_t hz)
{
  uint32_t x1,x2;

  x1=local_apic->timer_ccr.count; /*get current counter*/
  local_apic->timer_icr.count=fil; /*fillful initial counter */

  while(local_apic->timer_icr.count==x1) /* wait while cycle will be end */
    ;

  x1=local_apic->timer_ccr.count; /*get current counter*/
  usleep(1000000/hz); /*delay*/
  x2=local_apic->timer_ccr.count; /*again get current counter to see difference*/

  kprintf("delay loop = %d \n",x1-x2);
  delay_loop=x1-x2;
  /*ok, let's write a difference to icr*/
  local_apic->timer_icr.count=x1-x2; /* <-- this will tell us how much ticks we're really need */
}

extern void i8254_suspend(void);

void apic_timer_hack(void)
{
  local_apic->timer_icr.count=delay_loop;
}

#if 0
void test_handler(uint64_t irq)
{
  if(irq==17) {
    local_apic->timer_icr.count=delay_loop;
    local_apic_send_eoi();
    timer_tick();
  } else
    i8259a_ack_irq(irq);
  scheduler_tick();
  return;
}
#endif

void local_apic_timer_init(void)
{
  apic_lvt_timer_t lvt_timer=local_apic->lvt_timer;
  int i;

  i8254_suspend(); /* suspend general intel timer - bye bye, simple and pretty one, welcome to apic ...*/

  /* calibrate timer delimeter */
  __local_apic_timer_calibrate(32);
  /*calibrate to hz*/
  local_apic_timer_calibrate(HZ);
  /* setup timer vector  */
  lvt_timer.vector=0x31; 
  /* set periodic mode (set bit to 1) */
  lvt_timer.timer_mode = 0x1;
  /* enable timer */
  lvt_timer.mask=0x0;
  local_apic->lvt_timer.reg=lvt_timer.reg;
#if 0  
  /*  for(i=48;i<50;i++) {*/
  if(  install_interrupt_gate(0x31,irq_entrypoints_array[0x31-0x20],0,0))
    kprintf("oops\n");
    //}
#endif
}

#ifdef CONFIG_SMP

extern void ap_boot(void);

uint32_t apic_send_ipi_init(uint8_t apicid)
{
  int i=0;
  apic_icr1_t icr1=local_apic->icr1;
  apic_icr2_t icr2=local_apic->icr2;

  icr2.dest=apicid;
  local_apic->icr2.reg=icr2.reg;  
  icr1.tx_mode=TXMODE_INIT;
  icr1.rx_mode=DMODE_PHY;
  icr1.level=0x1;
  icr1.trigger=0x1;
  icr1.shorthand=0x0;
  icr1.vector=0;
  local_apic->icr1.reg=icr1.reg;  

  usleep(20);

  icr1=local_apic->icr1;
  icr1.tx_mode=TXMODE_INIT;
  icr1.rx_mode=DMODE_PHY;
  icr1.level=0x0;
  icr1.trigger=0x1;
  icr1.shorthand=0x0;
  icr1.vector=0;
  local_apic->icr1.reg=icr1.reg;  
  usleep(10000);

  for(i=0;i<2;i++) { /* if we're have APIC not from 80486DX or higher, we're need to send it twice */
    icr1=local_apic->icr1;
    icr1.vector=(uint8_t)(((uintptr_t) ap_boot) >> 12);
    icr1.tx_mode=TXMODE_STARTUP;
    icr1.rx_mode=DMODE_PHY;
    icr1.level=0x1;
    icr1.trigger=0x1;
    icr1.shorthand=0x0;
    local_apic->icr1.reg=icr1.reg;  
    usleep(200);
  }
}

extern ptr_16_32_t protected_ap_gdtr;

void arch_smp_init(void)
{
  ptr_16_64_t gdtr;
  disable_all_irqs();

    /*  outb(0x70, 0xf); /* set BIOS area to don't make a POST on INIT signal */
    //  outb(0x71, 0xa);


  /* ok setup new gdt */
  protected_ap_gdtr.limit=GDT_ITEMS * sizeof(struct __descriptor);
  protected_ap_gdtr.base=((uintptr_t)&gdt[1][0]-0xffffffff80000000);
  gdtr.base=&gdt[1];

  apic_send_ipi_init(1);
  /*  while(local_apic->icr1.tx_status!=0x0)
      apic_send_ipi_init(1);*/
  enable_all_irqs();
}

#endif /* CONFIG_SMP */

