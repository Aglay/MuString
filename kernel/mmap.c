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
 * kernel/mmap.c: Kernel mmap syscall
 *
 */

#include <eza/arch/types.h>
#include <eza/swks.h>
#include <mlibc/kprintf.h>
#include <eza/arch/page.h>
#include <eza/scheduler.h>
#include <eza/process.h>
#include <eza/arch/mm_types.h>
#include <eza/arch/mm.h>
#include <eza/errno.h>
#include <mm/mm.h>
#include <mm/page.h>
#include <mm/pfalloc.h>
#include <mm/mmap.h>
#include <mlibc/kprintf.h>
#include <server.h>
#include <kernel/vm.h> 
#include <kernel/mman.h> 

#include <mm/mmpool.h>

/*TODO: ak, redesign it according to mm changes */

/* system call used to map memory pages, ``memory'' means a regular area */
status_t sys_mmap(uintptr_t addr,size_t size,uint32_t flags,shm_id_t fd,uintptr_t offset)
{
  page_frame_t *aframe;
  status_t e;
  task_t *task;
  int _flags = MAP_USER | MAP_EXEC;
#if 0
  mm_pool_t *pool;
#endif

#if 0
  kprintf(">>>>>>>>>> allocced %d pages, addr=%p, flags=%d, fd=%ld, offset=%p\n",
	  size,addr,flags,fd,offset);
#endif

  /* simple check */
  if(!addr || !size) {
    return -EINVAL;
  }
  task=current_task();

  if(fd>65535) {
#if 0
    kprintf("mmap() no fd\n");
#endif
    if(flags & MMAP_PHYS && offset) {
      /* TODO: ak, check rights */
#if 0
      kprintf("mmap() yummie fuck\n");
#endif
      e=mmap(task->page_dir, addr, virt_to_pframe_id((void *)offset), size, MAP_USER | MAP_RW | MAP_EXEC | MMAP_NONCACHABLE);
      if(e!=0) return e;
#if 0
      kprintf("mmap() OK\n");
#endif
      return 0;
    }
#if 0
    kprintf("mmap() try to alloc\n");
    for_each_mm_pool(pool) {
      kprintf("free pages: %d\n", atomic_get(&pool->free_pages));
    }
#endif
    aframe=alloc_pages(size,AF_PGEN);
    if(!aframe)
      return -ENOMEM; /* no free pages to allocate */

    if(flags & MMAP_RW) /*map_rd map_rdonly*/
      _flags |= MAP_RW;
    else if(flags & MMAP_RDONLY)
      _flags|= MAP_READ;

    e=mmap(task->page_dir, addr, pframe_number(aframe), size, _flags);
    if(e!=0) return e;
  } else 
    return -ENOSYS;
  


  return 0;
}

