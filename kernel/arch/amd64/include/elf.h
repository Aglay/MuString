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
 * include/mstring/amd64/elf.c: elf architecture depended definion
 *
 */

#ifndef __EZA_ARCH_ELF_H__
#define __EZA_ARCH_ELF_H__

#include <arch/types.h>
#include <kernel/elf.h>

typedef elf64_t elf_head_t;
typedef elf64_pr_t elf_pr_t;
typedef elf64_sh_t elf_sh_t;

#endif

