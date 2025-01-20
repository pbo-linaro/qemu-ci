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

#include "util/atc.h"


#define PAGE_TABLE_ENTRY_SIZE 8

/* a pasid is hashed using the identity function */
static guint atc_pasid_key_hash(gconstpointer v)
{
    return (guint)(uintptr_t)v; /* pasid */
}

/* pasid equality */
static gboolean atc_pasid_key_equal(gconstpointer v1, gconstpointer v2)
{
    return v1 == v2;
}

/* Hash function for IOTLB entries */
static guint atc_addr_key_hash(gconstpointer v)
{
    hwaddr addr = (hwaddr)v;
    return (guint)((addr >> 32) ^ (addr & 0xffffffffU));
}

/* Equality test for IOTLB entries */
static gboolean atc_addr_key_equal(gconstpointer v1, gconstpointer v2)
{
    return (hwaddr)v1 == (hwaddr)v2;
}

static void atc_address_space_free(void *as)
{
    g_hash_table_unref(as);
}

/* return log2(val), or UINT8_MAX if val is not a power of 2 */
static uint8_t ilog2(uint64_t val)
{
    uint8_t result = 0;
    while (val != 1) {
        if (val & 1) {
            return UINT8_MAX;
        }

        val >>= 1;
        result += 1;
    }
    return result;
}

ATC *atc_new(uint64_t page_size, uint8_t address_width)
{
    ATC *atc;
    uint8_t log_page_size = ilog2(page_size);
    /* number of bits each used to store all the intermediate indexes */
    uint64_t addr_lookup_indexes_size;

    if (log_page_size == UINT8_MAX) {
        return NULL;
    }
    /*
     * We only support page table entries of 8 (PAGE_TABLE_ENTRY_SIZE) bytes
     * log2(page_size / 8) = log2(page_size) - 3
     * is the level offset
     */
    if (log_page_size <= 3) {
        return NULL;
    }

    atc = g_new0(ATC, 1);
    atc->address_spaces = g_hash_table_new_full(atc_pasid_key_hash,
                                                atc_pasid_key_equal,
                                                NULL, atc_address_space_free);
    atc->level_offset = log_page_size - 3;
    /* at this point, we know that page_size is a power of 2 */
    atc->min_addr_mask = page_size - 1;
    addr_lookup_indexes_size = address_width - log_page_size;
    if ((addr_lookup_indexes_size % atc->level_offset) != 0) {
        goto error;
    }
    atc->levels = addr_lookup_indexes_size / atc->level_offset;
    atc->page_size = page_size;
    return atc;

error:
    g_free(atc);
    return NULL;
}

static inline GHashTable *atc_get_address_space_cache(ATC *atc, uint32_t pasid)
{
    return g_hash_table_lookup(atc->address_spaces,
                               (gconstpointer)(uintptr_t)pasid);
}

void atc_create_address_space_cache(ATC *atc, uint32_t pasid)
{
    GHashTable *as_cache;

    as_cache = atc_get_address_space_cache(atc, pasid);
    if (!as_cache) {
        as_cache = g_hash_table_new_full(atc_addr_key_hash,
                                         atc_addr_key_equal,
                                         NULL, g_free);
        g_hash_table_replace(atc->address_spaces,
                             (gpointer)(uintptr_t)pasid, as_cache);
    }
}

void atc_delete_address_space_cache(ATC *atc, uint32_t pasid)
{
    g_hash_table_remove(atc->address_spaces, (gpointer)(uintptr_t)pasid);
}

int atc_update(ATC *atc, IOMMUTLBEntry *entry)
{
    IOMMUTLBEntry *value;
    GHashTable *as_cache = atc_get_address_space_cache(atc, entry->pasid);
    if (!as_cache) {
        return -ENODEV;
    }
    value = g_memdup2(entry, sizeof(*value));
    g_hash_table_replace(as_cache, (gpointer)(entry->iova), value);
    return 0;
}

IOMMUTLBEntry *atc_lookup(ATC *atc, uint32_t pasid, hwaddr addr)
{
    IOMMUTLBEntry *entry;
    hwaddr mask = atc->min_addr_mask;
    hwaddr key = addr & (~mask);
    GHashTable *as_cache = atc_get_address_space_cache(atc, pasid);

    if (!as_cache) {
        return NULL;
    }

    /*
     * Iterate over the possible page sizes and try to find a hit
     */
    for (uint8_t level = 0; level < atc->levels; ++level) {
        entry = g_hash_table_lookup(as_cache, (gconstpointer)key);
        if (entry && (mask == entry->addr_mask)) {
            return entry;
        }
        mask = (mask << atc->level_offset) | ((1 << atc->level_offset) - 1);
        key = addr & (~mask);
    }

    return NULL;
}

static gboolean atc_invalidate_entry_predicate(gpointer key, gpointer value,
                                               gpointer user_data)
{
    IOMMUTLBEntry *entry = (IOMMUTLBEntry *)value;
    IOMMUTLBEntry *target = (IOMMUTLBEntry *)user_data;
    hwaddr target_mask = ~target->addr_mask;
    hwaddr entry_mask = ~entry->addr_mask;
    return ((target->iova & target_mask) == (entry->iova & target_mask)) ||
           ((target->iova & entry_mask) == (entry->iova & entry_mask));
}

void atc_invalidate(ATC *atc, IOMMUTLBEntry *entry)
{
    GHashTable *as_cache = atc_get_address_space_cache(atc, entry->pasid);
    if (!as_cache) {
        return;
    }
    g_hash_table_foreach_remove(as_cache,
                                atc_invalidate_entry_predicate,
                                entry);
}

void atc_destroy(ATC *atc)
{
    g_hash_table_unref(atc->address_spaces);
}

size_t atc_get_max_number_of_pages(ATC *atc, hwaddr addr, size_t length)
{
    hwaddr page_mask = ~(atc->min_addr_mask);
    size_t result = (length / atc->page_size);
    if ((((addr & page_mask) + length - 1) & page_mask) !=
        ((addr + length - 1) & page_mask)) {
        result += 1;
    }
    return result + (length % atc->page_size != 0 ? 1 : 0);
}

void atc_reset(ATC *atc)
{
    g_hash_table_remove_all(atc->address_spaces);
}
