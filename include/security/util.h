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
 * (c) Copyright 2009 Michael Tsymbalyuk <mtzaurus@gmail.com>
 *
 * include/security/util.h: helper functions for the kernel security facility.
 */

#include <mstring/types.h>
#include <security/security.h>
#include <mstring/task.h>
#include <arch/current.h>

#define S_GET_TASK_OBJ(t)  (&(t)->sobject->sobject)
#define S_GET_INVOKER() S_GET_TASK_OBJ(current_task())
#define S_GET_PORT_OBJ(p) &(p)->sobject
