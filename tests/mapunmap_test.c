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
 * (c) Copyright 2009 Dan Kruchinin <dan.kruchinin@gmail.com>
 *
 * tests/mapunmap_test.c: General map/unmap test
 *
 */

#include <test.h>
#include <ds/iterator.h>
#include <mm/page.h>
#include <mm/mmpool.h>
#include <mm/page_alloc.h>
#include <mm/vmm.h>
#include <mm/pfi.h>
#include <mstring/task.h>
#include <kernel/syscalls.h>
#include <mstring/types.h>

#define MAPUNMAP_TEST_ID "Map/Unmap test"
#define TC_MAP_ADDR PAGE_ALIGN((KERNEL_BASE + ((ulong_t)num_phys_pages << PAGE_WIDTH) * 2))
#define TC_MAP_ADDR_FAR (TC_MAP_ADDR + ((ulong_t)num_phys_pages << PAGE_WIDTH) * 4)

static bool is_completed = false;

static void __check_mapped(test_framework_t *tf, uintptr_t start_addr,
                           ulong_t num_pages, ulong_t start_idx)
{
  page_idx_t idx = start_idx;
  page_frame_iterator_t pfi;
  ITERATOR_CTX(page_frame, PF_ITER_PTABLE) pfi_ptable_ctx;

  pfi_ptable_init(&pfi, &pfi_ptable_ctx, &kernel_rpd, start_addr, num_pages);
  iterate_forward(&pfi) {
    if (pfi.pf_idx != idx) {
      tf->printf("Mapping %p -> %p: Expected page index %d, but got %d\n",
                 start_addr, start_addr + (num_pages << PAGE_WIDTH), start_idx, pfi.pf_idx);
      tf->abort();
    }

    idx++;
  }
  if ((idx - start_idx) != num_pages) {
    tf->printf("%d - %d != %d\n", idx, start_idx, num_pages);
    tf->abort();
  }
}

static void __check_refcounts(test_framework_t *tf, page_frame_t *pages, int refc)
{
  page_frame_t *pf;
  list_node_t *iter;
  list_head_t head;

  list_init_head(&head);
  list_set_head(&head, &pages->chain_node);
  list_for_each(&head, iter) {
    pf = list_entry(iter, page_frame_t, chain_node);
    if (atomic_get(&pf->refcount) != refc) {
      tf->printf("Page idx %d: refcount != %d(%d)\n", pframe_number(pf), refc,
                 atomic_get(&pf->refcount));
      tf->failed();
    }
  }
}

static void tc_map_unmap_core(void *ctx)
{
  test_framework_t *tf = ctx;
  mm_pool_t *pool = POOL_GENERAL();
  ulong_t num_pages = pool->allocator.max_block_size - 1;
  int ret, i;
  page_frame_t *pages;
  page_idx_t sz;
  page_frame_iterator_t pfi;
  ITERATOR_CTX(page_frame, PF_ITER_LIST) pfi_list_ctx;
  
  pages = alloc_pages(num_pages, AF_ZERO);
  if (!pages) {
    tf->printf("Failed to allocate %d pages!\n", num_pages);
    tf->abort();
  }
  
  tf->printf("Creating mapping: %p -> %p\n", TC_MAP_ADDR, TC_MAP_ADDR + (num_pages << PAGE_WIDTH));
  ret = mmap_kern(TC_MAP_ADDR, pframe_number(pages), num_pages, KMAP_KERN | KMAP_READ | KMAP_WRITE);  
  if (ret) {
    tf->printf("Failed to map %d pages from %p to %p\n",
               atomic_get(&pool->free_pages), TC_MAP_ADDR, TC_MAP_ADDR + (num_pages << PAGE_WIDTH));
    tf->abort();
  }
  
  __check_mapped(tf, TC_MAP_ADDR, num_pages, pframe_number(pages));
  __check_refcounts(tf, pages, 0);
  tf->printf("Unmap mapped pages range [%p -> %p]\n", TC_MAP_ADDR, TC_MAP_ADDR + (num_pages << PAGE_WIDTH));
  munmap_kern(TC_MAP_ADDR, num_pages);
  __check_refcounts(tf, pages, 0);

  tf->printf("Remmaping mapping: %p -> %p\n", TC_MAP_ADDR, TC_MAP_ADDR + (num_pages << PAGE_WIDTH));
  ret = mmap_kern(TC_MAP_ADDR, pframe_number(pages), num_pages, KMAP_READ | KMAP_WRITE);  
  if (ret) {
    tf->printf("Failed to map %d pages from %p to %p\n",
               atomic_get(&pool->free_pages), TC_MAP_ADDR, TC_MAP_ADDR + (num_pages << PAGE_WIDTH));
    tf->abort();
  }

  *(int *)(TC_MAP_ADDR + ((num_pages - 1) << PAGE_WIDTH)) = 666;
  __check_refcounts(tf, pages, 1);
  tf->printf("Unmap mapped pages range [%p -> %p]\n", TC_MAP_ADDR, TC_MAP_ADDR + (num_pages << PAGE_WIDTH));
  munmap_kern(TC_MAP_ADDR, num_pages);

  tf->printf("Checking page-table recovery due mmap fail.\n");
  num_pages = atomic_get(&pool->free_pages) - 2;
  sz = atomic_get(&pool->free_pages);
  tf->printf("Allocating non-continous block of %d pages. (%d avail)\n",
             num_pages, atomic_get(&pool->free_pages));
  pages = alloc_pages(num_pages, AF_USER | AF_ZERO);
  if (!pages) {
    tf->printf("Failed to allocate %d non-continous pages.\n", num_pages);
    tf->failed();
  }

  tf->printf("Creating mapping: %p - %p\n", TC_MAP_ADDR_FAR, TC_MAP_ADDR_FAR + (num_pages << PAGE_WIDTH));
  pfi_list_init(&pfi, &pfi_list_ctx, &pages->chain_node, pages->chain_node.prev);
  i = 0;
  iterate_forward(&pfi) {
    i++;
  }
  if (i != num_pages) {
    tf->printf("Page block iterator failed: %d != %d\n", i, num_pages);
    tf->abort();
  }

  iter_first(&pfi);
  tf->printf("before mmap => %d\n", atomic_get(&pool->free_pages));
  ret = __mmap_core(&kernel_rpd, TC_MAP_ADDR_FAR, num_pages, &pfi, KMAP_READ | KMAP_WRITE | KMAP_EXEC);
  if (ret != -ENOMEM) {
    tf->printf("__mmap_core returned %d, but -ENOMEM(%d) was expected.\n", ret, -ENOMEM);
    tf->failed();
  }
  iterate_forward(&pfi) {
    if (atomic_get(&pframe_by_number(pfi.pf_idx)->refcount)) {
      tf->printf("Page frame %d has unexpected refcount %d\n",
                 pfi.pf_idx, atomic_get(&pframe_by_number(pfi.pf_idx)->refcount));
      tf->failed();
    }
  }
  
  tf->printf("after mmap => %d\n", atomic_get(&pool->free_pages));
  free_pages_chain(pages);
  if (atomic_get(&pool->free_pages) != sz) {
    tf->printf("It seems that mapping recovery wasn't fully completed.\n");
    tf->printf("Expected %d pages to be free, but %d free indeed.\n",
               sz, atomic_get(&pool->free_pages));
    tf->failed();
  }
  
  tf->printf("Done\n");
  is_completed = true;
  sys_exit(0);
}

static void tc_run(test_framework_t *tf, void *unused)
{
  if (kernel_thread(tc_map_unmap_core, tf, NULL)) {
    tf->printf("Can't create kernel thread!");
    tf->abort();
  }

  tf->test_completion_loop(MAPUNMAP_TEST_ID, &is_completed);
}

static bool tc_initialize(void **unused)
{
  return true;
}

static void tc_deinitialize(void *unused)
{
}


testcase_t mapunmap_tc = {
  .id = MAPUNMAP_TEST_ID,
  .initialize = tc_initialize,
  .run = tc_run,
  .deinitialize = tc_deinitialize,
};
