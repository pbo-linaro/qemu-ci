/*
 * QEMU AVR CPU
 *
 * Copyright (c) 2016-2020 Michael Rolnik
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see
 * <http://www.gnu.org/licenses/lgpl-2.1.html>
 */

#ifndef AVR_CPU_PARAM_H
#define AVR_CPU_PARAM_H

#define TARGET_PAGE_BITS_VARY
#define TARGET_PAGE_BITS_MIN 10

/*
 * The real value for TARGET_PHYS_ADDR_SPACE_BITS is 24, but selecting
 * an overly small value will assert in tb-maint.c when selecting the
 * shape of the page_table tree.  This allows an 8k page size.
 */
#define TARGET_PHYS_ADDR_SPACE_BITS 28
#define TARGET_VIRT_ADDR_SPACE_BITS 24

#define TCG_GUEST_DEFAULT_MO 0

#endif
