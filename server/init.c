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
 * (c) Copyright 2005,2008 Tirra <madtirra@jarios.org>
 *
 * server/init.c: servers initialization and running
 *
 */

#include <eza/arch/types.h>
#include <eza/scheduler.h>
#include <eza/process.h>
#include <eza/arch/mm.h>
#include <mm/page.h>
#include <mm/pfalloc.h>
#include <mm/vmm.h>
#include <eza/arch/elf.h>
#include <eza/kconsole.h>
#include <eza/errno.h>
#include <kernel/elf.h>
#include <mlibc/kprintf.h>
#include <server.h>
#include <mlibc/unistd.h>
#include <eza/process.h>
#include <eza/ptd.h>
#include <eza/gc.h>
#include <config.h>

long initrd_start_page,initrd_num_pages;

#ifndef CONFIG_TEST

static int __create_task_mm(task_t *task, int num)
{
  vmm_t *vmm = task->task_mm;
  memobj_t *memobj = &null_memobj;
  uintptr_t code;
  size_t code_size,data_size,text_size,bss_size;
  ulong_t *pp;
  elf_head_t ehead;
  elf_pr_t epr;
  elf_sh_t esh;
  uintptr_t data_bss,bss_virt,ustack_top;
  size_t real_code_size=0,real_data_size=0;
  size_t last_data_size,real_data_offset=0;
  size_t last_offset,last_sect_size,last_data_offset;
  long r;
  int i;
  per_task_data_t *ptd;

  code_size=init.server[num].size>>PAGE_WIDTH; /*it's a code size in pages */
  code_size++;

  code=init.server[num].addr; /* we're have a physical address of code here */
  code>>=PAGE_WIDTH; /* get page number */
  pp=pframe_id_to_virt(code);
    /**
     * ELF header   
     * unsigned char e_ident[EI_NIDENT];  ELF64 magic number 
     * uint16_t e_type;  elf type 
     * uint16_t e_machine;  elf required architecture 
     * uint32_t e_version;  elf object file version 
     * uintptr_t e_entry;  entry point virtual address 
     * uintptr_t e_phoff;  program header file offset 
     * uintptr_t e_shoff;  section header file offset 
     * uint32_t e_flags;  processor specific object tags 
     * uint16_t e_ehsize;  elf header size in bytes 
     * uint16_t e_phentsize;  program header table entry size 
     * uint16_t e_phnum;  program header count 
     * uint16_t e_shentsize;  section header table entry size 
     * uint16_t e_shnum;  section header count 
     * uint16_t e_shstrndx;  section header string table index 
     */
  /* read elf headers */
  memcpy(&ehead,pframe_id_to_virt(code),sizeof(elf_head_t));
  /* printf elf header info */
  /*kprintf("ELF header(%s): %d type, %d mach, %d version\n",ehead.e_ident,ehead.e_type,ehead.e_machine,ehead.e_version);
  kprintf("Entry: %p,Image off: %p,sect off:%p\n",ehead.e_entry,ehead.e_phoff,ehead.e_shoff);*/

  for(i=0;i<ehead.e_phnum;i++) {
    /* read program size */
    memcpy(&epr,pframe_id_to_virt(code)+sizeof(ehead)+i*(ehead.e_phentsize),sizeof(epr));
    /*kprintf("PHeader(%d): offset: %p\nvirt: %p\nphy: %p\n",
	    i,epr.p_offset,epr.p_vaddr,epr.p_paddr);*/

  }
  for(i=0;i<ehead.e_shnum;i++) {
    memcpy(&esh,pframe_id_to_virt(code)+ehead.e_shoff+i*(ehead.e_shentsize),sizeof(esh));
    if(esh.sh_size!=0) {
/*      kprintf("SHeader(%d): shaddr: %p\nshoffset:%p\n",i,esh.sh_addr,esh.sh_offset);*/
      if(esh.sh_flags & ESH_ALLOC && esh.sh_type==SHT_PROGBITS) {
	if(esh.sh_flags & ESH_EXEC) {
	  real_code_size+=esh.sh_size;
	  last_offset=esh.sh_addr;
	  last_sect_size=esh.sh_size;
	} else if(esh.sh_flags & ESH_WRITE) {
	  real_data_size+=esh.sh_size;
	  if(real_data_offset==0) 
	    real_data_offset=esh.sh_addr;
	  last_data_offset=esh.sh_addr;
	  last_data_size=esh.sh_size;
	} else { /* rodata */
	  real_code_size+=esh.sh_size;
	  last_offset=esh.sh_addr;
	  last_sect_size=esh.sh_size;
	}
/*	atom_usleep(100);*/
      } else if(esh.sh_flags & ESH_ALLOC && esh.sh_type==SHT_NOBITS) { /* seems to be an bss section */
	bss_virt=esh.sh_addr; 
	bss_size=esh.sh_size;
      }
    }
  }

  /* print debug info */
/*  kprintf("Code: real size: %d, last_offset= %p, last section size= %d\n",
	  real_code_size,last_offset,last_sect_size);  code parsed values */
/*  kprintf("Data: real size: %d, last offset= %p, last section size= %d\nData offset: %p\n",
	  real_data_size,last_data_offset,last_data_size,real_data_offset);*/
  /* calculate text */
  code=init.server[num].addr+0x1000;
  text_size=real_code_size>>PAGE_WIDTH;
  if(real_code_size%PAGE_SIZE)    text_size++;
  data_bss=init.server[num].addr+real_data_offset-0x1000000;
/*  kprintf("data bss: %p\n text: %p\n",data_bss,code);*/
  data_size=real_data_size>>PAGE_WIDTH;
  if(real_data_size%PAGE_SIZE)    data_size++;
  /* calculate bss */
  if(bss_size%PAGE_SIZE) {
    bss_size>>=PAGE_WIDTH;
    bss_size++;
  } else 
    bss_size>>=PAGE_WIDTH;

  /*  kprintf("elf entry -> %p\n",ehead.e_entry); */

  /*remap pages*/
  r = vmrange_map(memobj, vmm, USPACE_VA_BOTTOM, text_size, VMR_READ | VMR_EXEC | VMR_PRIVATE | VMR_FIXED, 0);
  if (!PAGE_ALIGN(r))
    return r;
  r = mmap_core(task_get_rpd(task), USPACE_VA_BOTTOM, code >> PAGE_WIDTH, text_size, KMAP_READ | KMAP_EXEC);
  if (r)
    return r;

  //kprintf("TEXT: %p -> %p\n", USPACE_VA_BOTTOM, USPACE_VA_BOTTOM + (text_size << PAGE_WIDTH));
  r = vmrange_map(memobj, vmm, real_data_offset, data_size, VMR_READ | VMR_WRITE | VMR_PRIVATE | VMR_FIXED, 0);
  if (!PAGE_ALIGN(r))
    return r;
  r = mmap_core(task_get_rpd(task), real_data_offset, data_bss >> PAGE_WIDTH, data_size, KMAP_READ | KMAP_WRITE);
  if (r)
    return r;

  //kprintf("DATA: %p -> %p\n", real_data_offset, real_data_offset + (data_size << PAGE_WIDTH));
  /* Create a BSS area. */
  r = vmrange_map(memobj, vmm, bss_virt, bss_size,
                  VMR_READ | VMR_WRITE | VMR_PRIVATE | VMR_FIXED | VMR_POPULATE, 0);
  if(!PAGE_ALIGN(r)) {
    return r;
  }

  r = vmrange_map(memobj, vmm, USPACE_VA_TOP - 0x40000, USER_STACK_SIZE,
                  VMR_READ | VMR_WRITE | VMR_STACK | VMR_PRIVATE | VMR_POPULATE | VMR_FIXED, 0);
  /*r = mmap_core(task_get_rpd(task), USPACE_VA_TOP-0x40000, pframe_number(stack), USER_STACK_SIZE, KMAP_READ | KMAP_WRITE);*/
  if (!PAGE_ALIGN(r))
    return r;  
  /* Now allocate stack space for per-task user data. */
  ustack_top=USPACE_VA_TOP-0x40000+(USER_STACK_SIZE<<PAGE_WIDTH);
  ustack_top-=PER_TASK_DATA_SIZE;
  ptd=user_to_kernel_vaddr(task_get_rpd(task),ustack_top);
  ptd = (per_task_data_t *)((char *)ptd + (ustack_top - PAGE_ALIGN_DOWN(ustack_top)));
  if( !ptd ) {
    return -1;
  }
  ptd->ptd_addr=(uintptr_t)ustack_top;

  r=do_task_control(task,SYS_PR_CTL_SET_PERTASK_DATA,(uintptr_t)ustack_top);
  if( r ) {
    return r;
  }

  /* Insufficient return address to prevent task from returning to void. */
  ustack_top-=8;
  r=do_task_control(task,SYS_PR_CTL_SET_ENTRYPOINT,ehead.e_entry);
  r|=do_task_control(task,SYS_PR_CTL_SET_STACK,ustack_top);

  if (r)
    return r;

  /*  kprintf("Grub module: %p\n size: %ld\n",init.server[num].addr,init.server[num].size);*/

  return 0;
}

static void __server_task_runner(void *data)
{
  int i=server_get_num(),a;
  task_t *server;
  int r,sn;
  kconsole_t *kconsole=default_console();

  if( i > 0 ) {
    kprintf("[LAUNCHER] Starting servers: %d ... \n",i);
    //kconsole->disable();
  }

  for(sn=0,a=0;a<i;a++) {
    char *modvbase;

    modvbase=pframe_id_to_virt(init.server[a].addr>>PAGE_WIDTH);

    if( *(uint32_t *)modvbase == ELF_MAGIC ) { /* ELF module ? */
      ulong_t t;

      if( !sn ) { /* First module is always NS. */
        t = TASK_INIT;
      } else {
        t=0;
      }

      r=create_task(current_task(),t,TPL_USER,&server,NULL);
      if( r ) {
        panic("server_run_tasks(): Can't create task N %d !\n",a+1);
      }

      if( !sn ) {
        if( server->pid != 1 ) {
          panic( "server_run_tasks(): NameServer has improper PID: %d !\n",
                 server->pid );
        }
      }

      r=__create_task_mm(server,a);
      if( r ) {
        panic( "server_run_tasks(): Can't create memory space for core task N %d\n",
               a+1);
      }

#ifdef CONFIG_CORESERVERS_PERCPU_LAUNCH
      /* Perform initial CPU deployment and activate the server. */
      t=sn % CONFIG_NRCPUS;

      if( t != cpu_id() ) {
        sched_move_task_to_cpu(server,t);
      }
#endif
      r=sched_change_task_state(server,TASK_STATE_RUNNABLE);
      if( r ) {
        panic( "server_run_tasks(): Can't launch core task N%d !\n",a+1);
      }
      sn++;

      if( CONFIG_CORESERVERS_LAUNCH_DELAY >= 100 ) {
        sleep(CONFIG_CORESERVERS_LAUNCH_DELAY);
      }
    } else if( !strncmp(&modvbase[257],"ustar",5 ) ) { /* TAR-based ramdisk ? */
      long size;

      if( initrd_start_page ) {
        panic("Only one instance of initial RAM disk is allowed !");
      }

      initrd_start_page=init.server[a].addr>>PAGE_WIDTH;
      size=init.server[a].size>>PAGE_WIDTH;
      if( size & PAGE_MASK ) {
        size++;
      }
      initrd_num_pages=size;
    } else {
      panic("Unrecognized kernel module N %d !\n",a+1);
    }
  }
  kprintf("[LAUNCHER]: All servers started. Exiting ...\n");
  sys_exit(0);
}

void server_run_tasks(void)
{
  if( kernel_thread(__server_task_runner,NULL,NULL) ) {
    panic("Can't launch a Core Servers runner !");
  }
}

#else

void server_run_tasks(void)
{
  /* In test mode we do nothing. */
}

#endif /* !CONFIG_TEST */
