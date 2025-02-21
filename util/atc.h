/*
 * QEMU emulation of an ATC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef UTIL_ATC_H
#define UTIL_ATC_H

#include "qemu/osdep.h"
#include "exec/memory.h"

typedef struct ATC {
    GHashTable *address_spaces; /* Key : pasid, value : GHashTable */
    hwaddr min_addr_mask;
    uint64_t page_size;
    uint8_t levels;
    uint8_t level_offset;
} ATC;

/*
 * atc_new: Create an ATC.
 *
 * Return an ATC or NULL if the creation failed
 *
 * @page_size: #PCIDevice doing the memory access
 * @address_width: width of the virtual addresses used by the IOMMU (in bits)
 */
ATC *atc_new(uint64_t page_size, uint8_t address_width);

/*
 * atc_update: Insert or update an entry in the cache
 *
 * Return 0 if the operation succeeds, a negative error code otherwise
 *
 * The insertion will fail if the address space associated with this pasid
 * has not been created with atc_create_address_space_cache
 *
 * @atc: the ATC to update
 * @entry: the tlb entry to insert into the cache
 */
int atc_update(ATC *atc, IOMMUTLBEntry *entry);

/*
 * atc_create_address_space_cache: delare a new address space
 * identified by a PASID
 *
 * @atc: the ATC to update
 * @pasid: the pasid of the address space to be created
 */
void atc_create_address_space_cache(ATC *atc, uint32_t pasid);

/*
 * atc_delete_address_space_cache: delete an address space
 * identified by a PASID
 *
 * @atc: the ATC to update
 * @pasid: the pasid of the address space to be deleted
 */
void atc_delete_address_space_cache(ATC *atc, uint32_t pasid);

/*
 * atc_lookup: query the cache in a given address space
 *
 * @atc: the ATC to query
 * @pasid: the pasid of the address space to query
 * @addr: the virtual address to translate
 */
IOMMUTLBEntry *atc_lookup(ATC *atc, uint32_t pasid, hwaddr addr);

/*
 * atc_invalidate: invalidate an entry in the cache
 *
 * @atc: the ATC to update
 * @entry: the entry to invalidate
 */
void atc_invalidate(ATC *atc, IOMMUTLBEntry *entry);

/*
 * atc_destroy: delete an ATC
 *
 * @atc: the cache to be deleted
 */
void atc_destroy(ATC *atc);

/*
 * atc_get_max_number_of_pages: get the number of pages a memory operation
 * will access if all the pages concerned have the minimum size.
 *
 * This function can be used to determine the size of the result array to be
 * allocated when issuing an ATS request.
 *
 * @atc: the cache
 * @addr: start address
 * @length: number of bytes accessed from addr
 */
size_t atc_get_max_number_of_pages(ATC *atc, hwaddr addr, size_t length);

/*
 * atc_reset: invalidates all the entries stored in the ATC
 *
 * @atc: the cache
 */
void atc_reset(ATC *atc);

#endif
