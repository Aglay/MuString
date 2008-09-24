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
 * (c) Copyright 2008 Dan Kruchinin <dan.kruchinin@gmail.com>
 *
 * eza/amd64/mm/mm.c: Implementations of routines for initial memory remapping
 *                    using 4K pages (AMD64-specific).
 *
 */

#include <config.h>
#include <ds/iterator.h>
#include <ds/list.h>
#include <mlibc/kprintf.h>
#include <mlibc/string.h>
#include <mm/mm.h>
#include <mm/page.h>
#include <mm/mmpool.h>
#include <eza/kernel.h>
#include <eza/swks.h>
#include <eza/arch/mm.h>
#include <eza/arch/page.h>

/* Initial kernel top-level page directory record. */
uintptr_t _kernel_extended_end;
uint8_t e820count;
page_directory_t kernel_pt_directory;
uint8_t k_entries[PAGE_SIZE] __attribute__((aligned(PAGE_SIZE)));

static page_idx_t dma_pages = 0;
static uint64_t min_phys_addr = 0, max_phys_addr = 0;

static void initialize_kernel_page_directory(void)
{
  initialize_page_directory(&kernel_pt_directory);
  kernel_pt_directory.entries = k_entries;
}

#ifdef DEBUG_MM
static void verify_mapping(const char *descr, uintptr_t start_addr,
                          page_idx_t num_pages, page_idx_t start_idx)
{
  page_idx_t i, t;
  char *ptr = (char *)start_addr;
  bool ok = true;

  kprintf(" Verifying %s mapping...", descr);
  for( i = 0; i < num_pages; i++ ) {
    t = mm_pin_virtual_address(&kernel_pt_directory,(uintptr_t)ptr);
    if(t != start_idx) {
      ok = false;
      break;
    }

    start_idx++;
    ptr += PAGE_SIZE;
  }

  if (ok) {
    kprintf(" %*s\n", 14 - strlen(descr), "[OK]");
    return;
  }
  
  kprintf(" %*s\n", 18 - strlen(descr), "[FAILED]");
  panic("[!!!] 0x%X: page mismatch ! found idx: 0x%X, expected: 0x%X\n",
        ptr, t, start_idx);
}
#else
#define verify_mapping(descr, start_addr, num_pages, start_idx)
#endif /* DEBUG_MM */

static void scan_phys_mem(void)
{
  int idx, found;
  kprintf( "E820 memory map:\n" ); 
  char *types[] = { "(unknown)", "(usable)", "(reserved)", "(ACPI reclaimable)",
                    "(ACPI non-volatile)", "(BAD)" };

  for( idx = 0, found = 0; idx < e820count; idx++ ) {
    e820memmap_t *mmap = &e820table[idx];
    uint64_t length = ((uintptr_t)mmap->length_high << 32) | mmap->length_low;
    char *type;

    if( mmap->type <= 5 ) {
      type = types[mmap->type];
    } else {
      type = types[0];
    }

    kprintf(" BIOS-e820: %#.8x - %#.8x %s\n",
            mmap->base_address, mmap->base_address + length, type);

    if( !found && mmap->base_address == KERNEL_PHYS_START && mmap->type == 1 ) {
      min_phys_addr = 0;
      max_phys_addr = mmap->base_address + length;
      found = 1;
    }
  }

  if( !found ) {
    panic( "detect_physical_memory(): No valid E820 memory maps found for main physical memory area !\n" );
  }

  if( max_phys_addr <= min_phys_addr ||
      ((min_phys_addr - max_phys_addr) <= MIN_PHYS_MEMORY_REQUIRED )) {
    panic( "detect_physical_memory(): Insufficient E820 memory map found for main physical memory area !\n" );
  }

#ifndef CONFIG_IOMMU
  /* Setup DMA zone. */
  dma_pages = _mb2b(16) >> PAGE_WIDTH;
#endif /* CONFIG_IOMMU */
}

void arch_mm_init(void)
{
  kprintf("[MM] Scanning physical memory...\n");
  scan_phys_mem();
  swks.mem_total_pages = max_phys_addr >> PAGE_WIDTH;
  page_frames_array = KERNEL_FIRST_FREE_ADDRESS;  
  _kernel_extended_end =
    PAGE_ALIGN((uintptr_t)page_frames_array + sizeof(page_frame_t) * swks.mem_total_pages);
  kprintf(" Scanned: %ldM, %ld pages\n", (long)_b2mb(max_phys_addr - min_phys_addr),
          (long)swks.mem_total_pages);  
}

static int prepare_page(page_idx_t idx, ITERATOR_CTX(page_frame, PF_ITER_ARCH) *ctx)
{
  e820memmap_t *mmap = ctx->mmap;
  uintptr_t mmap_end = mmap->base_address +
    (((uintptr_t)(mmap->length_high) << 32) | mmap->length_low);
  uint32_t mmap_type = mmap->type;
  page_frame_t *page = page_frames_array + idx;

  memset(page, 0, sizeof(*page));
  page->idx = idx;
  if (page->idx < dma_pages)
    page->flags = PF_PDMA;
  else
    page->flags = PF_PGEN;
  if ((uintptr_t)pframe_phys_addr(page) > mmap_end) { /* switching to the next e820 map */
    if (ctx->e820id < e820count) {
      ctx->mmap = mmap = &e820table[++ctx->e820id];
      mmap_end = mmap->base_address + (((uintptr_t)mmap->length_high << 32) | mmap->length_low);
      mmap_type = mmap->type;
    }
    else { /* it seems that we've received a page with invalid idx... */
      return -1;
    }
  }
  if ((mmap_type != E820_USABLE) || is_kernel_addr(pframe_to_virt(page)) ||
      (page->idx < LAST_BIOS_PAGE)) {
    page->flags |= PF_RESERVED;
  }
  
  return 0;
}

static void __pfiter_first(page_frame_iterator_t *pfi)
{
  ITERATOR_CTX(page_frame, PF_ITER_ARCH) *ctx;

  ASSERT(pfi->type == PF_ITER_ARCH);
  pfi->pf_idx = 0;
  pfi->state = ITER_RUN;
  ctx = iter_fetch_ctx(pfi);
  ctx->mmap = &e820table[0];
  ctx->e820id = 0;
  if (prepare_page(pfi->pf_idx, ctx) < 0) {
    panic("e820 error: Can't recognize a page with index %d and physical address %p\n",
          pfi->pf_idx, pframe_phys_addr(page_frames_array + pfi->pf_idx));
  }
}

static void __pfiter_last(page_frame_iterator_t *pfi)
{
  panic("PF_ITER_ARCH doesn't support iteration to the last item of its set!");
}

static void __pfiter_next(page_frame_iterator_t *pfi)
{
  ITERATOR_CTX(page_frame, PF_ITER_ARCH) *ctx;

  ASSERT(pfi->type == PF_ITER_ARCH);
  if (pfi->pf_idx >= swks.mem_total_pages)
    pfi->state = ITER_STOP;
  else {
    ctx = iter_fetch_ctx(pfi);
    pfi->pf_idx++;
    if (prepare_page(pfi->pf_idx, ctx) < 0) {
      panic("e820 error: Can't recognize a page with index %d and physical address %p\n",
          pfi->pf_idx, pframe_phys_addr(page_frames_array + pfi->pf_idx));
    }
  }
}

static void __pfiter_prev(pfi)
{
  panic("PF_ITER_ARCH doesn't support iteration to the previous item of its set!");
}

void arch_mm_page_iter_init(page_frame_iterator_t *pfi, ITERATOR_CTX(page_frame, PF_ITER_ARCH) *ctx)
{
  pfi->first = __pfiter_first;
  pfi->last = __pfiter_last;
  pfi->prev = __pfiter_prev;
  pfi->next = __pfiter_next;
  pfi->pf_idx = PF_ITER_UNDEF_VAL;
  iter_init(pfi, PF_ITER_ARCH);
  memset(ctx, 0, sizeof(*ctx));
  iter_set_ctx(pfi, ctx);
}

void arch_mm_remap_pages(void)
{
  int ret;
  page_frame_iterator_t pfi;
  ITERATOR_CTX(page_frame, PF_ITER_INDEX) pfi_index_ctx;

  mm_init_pfiter_index(&pfi, &pfi_index_ctx, 1, IDENT_MAP_PAGES - 1);

  /* First, initialize kernel default page-table directory. */
  initialize_kernel_page_directory();
  kprintf(" PGD: Virt = 0x%x, Phys = 0x%x\n",
          kernel_pt_directory.entries, virt_to_phys(kernel_pt_directory.entries));

  /* Create identity mapping */
  ret = mm_map_pages(&kernel_pt_directory, &pfi, 0x1000, IDENT_MAP_PAGES - 1, MAP_KERNEL | MAP_RW);
  if(ret != 0)
    panic( "arch_mm_remap_pages(): Can't remap physical pages (DMA identical mapping) !" );

    verify_mapping("identity", 0x1000, IDENT_MAP_PAGES - 1, 1);
  
  /* Now we should remap all available physical memory starting at 'KERNEL_BASE'. */
  mm_init_pfiter_index(&pfi, &pfi_index_ctx, 0, swks.mem_total_pages - 1);
  ret = mm_map_pages(&kernel_pt_directory, &pfi, KERNEL_BASE,
                     swks.mem_total_pages, MAP_KERNEL | MAP_RW);
  if( ret != 0 ) {
    panic( "arch_mm_remap_pages(): Can't remap physical pages !" );
  }

  /* Verify that mappings are valid. */  
  verify_mapping("general", KERNEL_BASE, swks.mem_total_pages, 0);

  /* All CPUs must initially reload their CR3 registers with already
   * initialized Level-4 page directory.
   */
  arch_smp_mm_init(0);
}

void arch_smp_mm_init(int cpu)
{
  load_cr3( _k2p((uintptr_t)&kernel_pt_directory.entries[0]), 1, 1 );  
  kprintf("[MM] CPU #%d mm was initialized\n", cpu);  
}

/* AMD 64-specific function for zeroizing a page. */
void arch_clean_page(page_frame_t *frame)
{
  memset( pframe_to_virt(frame), 0, PAGE_SIZE );
}

