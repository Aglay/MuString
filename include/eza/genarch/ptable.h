#ifndef __GENARCH_PTABLE_H__
#define __GENARCH_PTABLE_H__

#include <ds/iterator.h>
#include <mm/page.h>
#include <eza/arch/ptable.h>
#include <mlibc/types.h>

int generic_ptable_map(rpd_t *rpd, uintptr_t va_from, page_idx_t npages,
                       page_frame_iterator_t *pfi, ptable_flags_t flags);
void generic_ptable_unmap(rpd_t *rpd, uintptr_t va_from, page_idx_t npages);
page_idx_t generic_vaddr2page_idx(rpd_t *rpd, uintptr_t vaddr);
page_frame_t *generic_create_pagedir(void);

#endif /* __GENARCH_PTABLE_H__ */