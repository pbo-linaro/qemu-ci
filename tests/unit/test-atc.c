/*
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

static inline bool tlb_entry_equal(IOMMUTLBEntry *e1, IOMMUTLBEntry *e2)
{
    if (!e1 || !e2) {
        return !e1 && !e2;
    }
    return e1->iova == e2->iova &&
            e1->addr_mask == e2->addr_mask &&
            e1->pasid == e2->pasid &&
            e1->perm == e2->perm &&
            e1->target_as == e2->target_as &&
            e1->translated_addr == e2->translated_addr;
}

static void assert_lookup_equals(ATC *atc, IOMMUTLBEntry *target,
                                 uint32_t pasid, hwaddr iova)
{
    IOMMUTLBEntry *result;
    result = atc_lookup(atc, pasid, iova);
    g_assert(tlb_entry_equal(result, target));
}

static void check_creation(uint64_t page_size, uint8_t address_width,
                           uint8_t levels, uint8_t level_offset,
                           bool should_work) {
    ATC *atc = atc_new(page_size, address_width);
    if (atc) {
        g_assert(atc->levels == levels);
        g_assert(atc->level_offset == level_offset);

        atc_destroy(atc);
        g_assert(should_work);
    } else {
        g_assert(!should_work);
    }
}

static void test_creation_parameters(void)
{
    check_creation(8, 39, 3, 9, false);
    check_creation(4095, 39, 3, 9, false);
    check_creation(4097, 39, 3, 9, false);
    check_creation(8192, 48, 0, 0, false);

    check_creation(4096, 38, 0, 0, false);
    check_creation(4096, 39, 3, 9, true);
    check_creation(4096, 40, 0, 0, false);
    check_creation(4096, 47, 0, 0, false);
    check_creation(4096, 48, 4, 9, true);
    check_creation(4096, 49, 0, 0, false);
    check_creation(4096, 56, 0, 0, false);
    check_creation(4096, 57, 5, 9, true);
    check_creation(4096, 58, 0, 0, false);

    check_creation(16384, 35, 0, 0, false);
    check_creation(16384, 36, 2, 11, true);
    check_creation(16384, 37, 0, 0, false);
    check_creation(16384, 46, 0, 0, false);
    check_creation(16384, 47, 3, 11, true);
    check_creation(16384, 48, 0, 0, false);
    check_creation(16384, 57, 0, 0, false);
    check_creation(16384, 58, 4, 11, true);
    check_creation(16384, 59, 0, 0, false);
}

static void test_single_entry(void)
{
    IOMMUTLBEntry entry = {
        .iova = 0x123456789000ULL,
        .addr_mask = 0xfffULL,
        .pasid = 5,
        .perm = IOMMU_RW,
        .translated_addr = 0xdeadbeefULL,
    };

    ATC *atc = atc_new(4096, 48);
    g_assert(atc);

    assert_lookup_equals(atc, NULL, entry.pasid,
                         entry.iova + (entry.addr_mask / 2));

    atc_create_address_space_cache(atc, entry.pasid);
    g_assert(atc_update(atc, &entry) == 0);

    assert_lookup_equals(atc, NULL, entry.pasid + 1,
                         entry.iova + (entry.addr_mask / 2));
    assert_lookup_equals(atc, &entry, entry.pasid,
                         entry.iova + (entry.addr_mask / 2));

    atc_destroy(atc);
}

static void test_single_entry_2(void)
{
    static uint64_t page_size = 4096;
    IOMMUTLBEntry e1 = {
        .iova = 0xabcdef200000ULL,
        .addr_mask = 0xfffULL,
        .pasid = 1,
        .perm = IOMMU_RW,
        .translated_addr = 0x5eedULL,
    };

    ATC *atc = atc_new(page_size , 48);
    atc_create_address_space_cache(atc, e1.pasid);
    atc_update(atc, &e1);

    assert_lookup_equals(atc, NULL, e1.pasid, 0xabcdef201000ULL);

    atc_destroy(atc);
}

static void test_page_boundaries(void)
{
    static const uint32_t pasid = 5;
    static const hwaddr page_size = 4096;

    /* 2 consecutive entries */
    IOMMUTLBEntry e1 = {
        .iova = 0x123456789000ULL,
        .addr_mask = page_size - 1,
        .pasid = pasid,
        .perm = IOMMU_RW,
        .translated_addr = 0xdeadbeefULL,
    };
    IOMMUTLBEntry e2 = {
        .iova = e1.iova + page_size,
        .addr_mask = page_size - 1,
        .pasid = pasid,
        .perm = IOMMU_RW,
        .translated_addr = 0x900df00dULL,
    };

    ATC *atc = atc_new(page_size, 48);

    atc_create_address_space_cache(atc, e1.pasid);
    /* creating the address space twice should not be a problem */
    atc_create_address_space_cache(atc, e1.pasid);

    atc_update(atc, &e1);
    atc_update(atc, &e2);

    assert_lookup_equals(atc, NULL, e1.pasid, e1.iova - 1);
    assert_lookup_equals(atc, &e1, e1.pasid, e1.iova);
    assert_lookup_equals(atc, &e1, e1.pasid, e1.iova + e1.addr_mask);
    g_assert((e1.iova + e1.addr_mask + 1) == e2.iova);
    assert_lookup_equals(atc, &e2, e2.pasid, e2.iova);
    assert_lookup_equals(atc, &e2, e2.pasid, e2.iova + e2.addr_mask);
    assert_lookup_equals(atc, NULL, e2.pasid, e2.iova + e2.addr_mask + 1);

    assert_lookup_equals(atc, NULL, e1.pasid + 10, e1.iova);
    assert_lookup_equals(atc, NULL, e2.pasid + 10, e2.iova);
    atc_destroy(atc);
}

static void test_huge_page(void)
{
    static const uint32_t pasid = 5;
    static const hwaddr page_size = 4096;
    IOMMUTLBEntry e1 = {
        .iova = 0x123456600000ULL,
        .addr_mask = 0x1fffffULL,
        .pasid = pasid,
        .perm = IOMMU_RW,
        .translated_addr = 0xdeadbeefULL,
    };
    hwaddr addr;

    ATC *atc = atc_new(page_size, 48);

    atc_create_address_space_cache(atc, e1.pasid);
    atc_update(atc, &e1);

    for (addr = e1.iova; addr <= e1.iova + e1.addr_mask; addr += page_size) {
        assert_lookup_equals(atc, &e1, e1.pasid, addr);
    }
    /* addr is now out of the huge page */
    assert_lookup_equals(atc, NULL, e1.pasid, addr);
    atc_destroy(atc);
}

static void test_pasid(void)
{
    hwaddr addr = 0xaaaaaaaaa000ULL;
    IOMMUTLBEntry e1 = {
        .iova = addr,
        .addr_mask = 0xfffULL,
        .pasid = 8,
        .perm = IOMMU_RW,
        .translated_addr = 0xdeadbeefULL,
    };
    IOMMUTLBEntry e2 = {
        .iova = addr,
        .addr_mask = 0xfffULL,
        .pasid = 2,
        .perm = IOMMU_RW,
        .translated_addr = 0xb001ULL,
    };
    uint16_t i;

    ATC *atc = atc_new(4096, 48);

    atc_create_address_space_cache(atc, e1.pasid);
    atc_create_address_space_cache(atc, e2.pasid);
    atc_update(atc, &e1);
    atc_update(atc, &e2);

    for (i = 0; i <= MAX(e1.pasid, e2.pasid) + 1; ++i) {
        if (i == e1.pasid || i == e2.pasid) {
            continue;
        }
        assert_lookup_equals(atc, NULL, i, addr);
    }
    assert_lookup_equals(atc, &e1, e1.pasid, addr);
    assert_lookup_equals(atc, &e1, e1.pasid, addr);
    atc_destroy(atc);
}

static void test_large_address(void)
{
    IOMMUTLBEntry e1 = {
        .iova = 0xaaaaaaaaa000ULL,
        .addr_mask = 0xfffULL,
        .pasid = 8,
        .perm = IOMMU_RW,
        .translated_addr = 0x5eeeeeedULL,
    };
    IOMMUTLBEntry e2 = {
        .iova = 0x1f00baaaaabf000ULL,
        .addr_mask = 0xfffULL,
        .pasid = e1.pasid,
        .perm = IOMMU_RW,
        .translated_addr = 0xdeadbeefULL,
    };

    ATC *atc = atc_new(4096, 57);

    atc_create_address_space_cache(atc, e1.pasid);
    atc_update(atc, &e1);
    atc_update(atc, &e2);

    assert_lookup_equals(atc, &e1, e1.pasid, e1.iova);
    assert_lookup_equals(atc, &e2, e2.pasid, e2.iova);
    atc_destroy(atc);
}

static void test_bigger_page(void)
{
    IOMMUTLBEntry e1 = {
        .iova = 0xaabbccdde000ULL,
        .addr_mask = 0x1fffULL,
        .pasid = 1,
        .perm = IOMMU_RW,
        .translated_addr = 0x5eeeeeedULL,
    };
    hwaddr i;

    ATC *atc = atc_new(8192, 43);

    atc_create_address_space_cache(atc, e1.pasid);
    atc_update(atc, &e1);

    i = e1.iova & (~e1.addr_mask);
    assert_lookup_equals(atc, NULL, e1.pasid, i - 1);
    while (i <= e1.iova + e1.addr_mask) {
        assert_lookup_equals(atc, &e1, e1.pasid, i);
        ++i;
    }
    assert_lookup_equals(atc, NULL, e1.pasid, i);
    atc_destroy(atc);
}

static void test_unknown_pasid(void)
{
    IOMMUTLBEntry e1 = {
        .iova = 0xaabbccfff000ULL,
        .addr_mask = 0xfffULL,
        .pasid = 1,
        .perm = IOMMU_RW,
        .translated_addr = 0x5eeeeeedULL,
    };

    ATC *atc = atc_new(4096, 48);
    g_assert(atc_update(atc, &e1) != 0);
    assert_lookup_equals(atc, NULL, e1.pasid, e1.iova);
    atc_destroy(atc);
}

static void test_invalidation(void)
{
    static uint64_t page_size = 4096;
    IOMMUTLBEntry e1 = {
        .iova = 0xaabbccddf000ULL,
        .addr_mask = 0xfffULL,
        .pasid = 1,
        .perm = IOMMU_RW,
        .translated_addr = 0x5eeeeeedULL,
    };
    IOMMUTLBEntry e2 = {
        .iova = 0xffe00000ULL,
        .addr_mask = 0x1fffffULL,
        .pasid = 1,
        .perm = IOMMU_RW,
        .translated_addr = 0xb000001ULL,
    };
    IOMMUTLBEntry e3;

    ATC *atc = atc_new(page_size , 48);
    atc_create_address_space_cache(atc, e1.pasid);

    atc_update(atc, &e1);
    assert_lookup_equals(atc, &e1, e1.pasid, e1.iova);
    atc_invalidate(atc, &e1);
    assert_lookup_equals(atc, NULL, e1.pasid, e1.iova);

    atc_update(atc, &e1);
    atc_update(atc, &e2);
    assert_lookup_equals(atc, &e1, e1.pasid, e1.iova);
    assert_lookup_equals(atc, &e2, e2.pasid, e2.iova);
    atc_invalidate(atc, &e2);
    assert_lookup_equals(atc, &e1, e1.pasid, e1.iova);
    assert_lookup_equals(atc, NULL, e2.pasid, e2.iova);

    /* invalidate a huge page by invalidating a small region */
    for (hwaddr addr = e2.iova; addr <= (e2.iova + e2.addr_mask);
         addr += page_size) {
        atc_update(atc, &e2);
        assert_lookup_equals(atc, &e2, e2.pasid, e2.iova);
        e3 = (IOMMUTLBEntry){
            .iova = addr,
            .addr_mask = page_size - 1,
            .pasid = e2.pasid,
            .perm = IOMMU_RW,
            .translated_addr = 0,
        };
        atc_invalidate(atc, &e3);
        assert_lookup_equals(atc, NULL, e2.pasid, e2.iova);
    }
    atc_destroy(atc);
}

static void test_delete_address_space_cache(void)
{
    static uint64_t page_size = 4096;
    IOMMUTLBEntry e1 = {
        .iova = 0xaabbccddf000ULL,
        .addr_mask = 0xfffULL,
        .pasid = 1,
        .perm = IOMMU_RW,
        .translated_addr = 0x5eeeeeedULL,
    };
    IOMMUTLBEntry e2 = {
        .iova = e1.iova,
        .addr_mask = 0xfffULL,
        .pasid = 2,
        .perm = IOMMU_RW,
        .translated_addr = 0x5eeeeeedULL,
    };

    ATC *atc = atc_new(page_size , 48);
    atc_create_address_space_cache(atc, e1.pasid);

    atc_update(atc, &e1);
    assert_lookup_equals(atc, &e1, e1.pasid, e1.iova);
    atc_invalidate(atc, &e2); /* unkown pasid : is a nop*/
    assert_lookup_equals(atc, &e1, e1.pasid, e1.iova);

    atc_create_address_space_cache(atc, e2.pasid);
    atc_update(atc, &e2);
    assert_lookup_equals(atc, &e1, e1.pasid, e1.iova);
    assert_lookup_equals(atc, &e2, e2.pasid, e2.iova);
    atc_invalidate(atc, &e1);
    /* e1 has been removed but e2 is still there */
    assert_lookup_equals(atc, NULL, e1.pasid, e1.iova);
    assert_lookup_equals(atc, &e2, e2.pasid, e2.iova);

    atc_update(atc, &e1);
    assert_lookup_equals(atc, &e1, e1.pasid, e1.iova);
    assert_lookup_equals(atc, &e2, e2.pasid, e2.iova);

    atc_delete_address_space_cache(atc, e2.pasid);
    assert_lookup_equals(atc, &e1, e1.pasid, e1.iova);
    assert_lookup_equals(atc, NULL, e2.pasid, e2.iova);
    atc_destroy(atc);
}

static void test_invalidate_entire_address_space(void)
{
    static uint64_t page_size = 4096;
    IOMMUTLBEntry e1 = {
        .iova = 0x1000ULL,
        .addr_mask = 0xfffULL,
        .pasid = 1,
        .perm = IOMMU_RW,
        .translated_addr = 0x5eedULL,
    };
    IOMMUTLBEntry e2 = {
        .iova = 0xfffffffff000ULL,
        .addr_mask = 0xfffULL,
        .pasid = 1,
        .perm = IOMMU_RW,
        .translated_addr = 0xbeefULL,
    };
    IOMMUTLBEntry e3 = {
        .iova = 0,
        .addr_mask = 0xffffffffffffffffULL,
        .pasid = 1,
        .perm = IOMMU_RW,
        .translated_addr = 0,
    };

    ATC *atc = atc_new(page_size , 48);
    atc_create_address_space_cache(atc, e1.pasid);

    atc_update(atc, &e1);
    atc_update(atc, &e2);
    assert_lookup_equals(atc, &e1, e1.pasid, e1.iova);
    assert_lookup_equals(atc, &e2, e2.pasid, e2.iova);
    atc_invalidate(atc, &e3);
    /* e1 has been removed but e2 is still there */
    assert_lookup_equals(atc, NULL, e1.pasid, e1.iova);
    assert_lookup_equals(atc, NULL, e2.pasid, e2.iova);

    atc_destroy(atc);
}

static void test_reset(void)
{
    static uint64_t page_size = 4096;
    IOMMUTLBEntry e1 = {
        .iova = 0x1000ULL,
        .addr_mask = 0xfffULL,
        .pasid = 1,
        .perm = IOMMU_RW,
        .translated_addr = 0x5eedULL,
    };
    IOMMUTLBEntry e2 = {
        .iova = 0xfffffffff000ULL,
        .addr_mask = 0xfffULL,
        .pasid = 2,
        .perm = IOMMU_RW,
        .translated_addr = 0xbeefULL,
    };

    ATC *atc = atc_new(page_size , 48);
    atc_create_address_space_cache(atc, e1.pasid);
    atc_create_address_space_cache(atc, e2.pasid);
    atc_update(atc, &e1);
    atc_update(atc, &e2);

    assert_lookup_equals(atc, &e1, e1.pasid, e1.iova);
    assert_lookup_equals(atc, &e2, e2.pasid, e2.iova);

    atc_reset(atc);

    assert_lookup_equals(atc, NULL, e1.pasid, e1.iova);
    assert_lookup_equals(atc, NULL, e2.pasid, e2.iova);
    atc_destroy(atc);
}

static void test_get_max_number_of_pages(void)
{
    static uint64_t page_size = 4096;
    hwaddr base = 0xc0fee000; /* aligned */
    ATC *atc = atc_new(page_size , 48);
    g_assert(atc_get_max_number_of_pages(atc, base, page_size / 2) == 1);
    g_assert(atc_get_max_number_of_pages(atc, base, page_size) == 1);
    g_assert(atc_get_max_number_of_pages(atc, base, page_size + 1) == 2);

    g_assert(atc_get_max_number_of_pages(atc, base + 10, 1) == 1);
    g_assert(atc_get_max_number_of_pages(atc, base + 10, page_size - 10) == 1);
    g_assert(atc_get_max_number_of_pages(atc, base + 10,
                                         page_size - 10 + 1) == 2);
    g_assert(atc_get_max_number_of_pages(atc, base + 10,
                                         page_size - 10 + 2) == 2);

    g_assert(atc_get_max_number_of_pages(atc, base + page_size - 1, 1) == 1);
    g_assert(atc_get_max_number_of_pages(atc, base + page_size - 1, 2) == 2);
    g_assert(atc_get_max_number_of_pages(atc, base + page_size - 1, 3) == 2);

    g_assert(atc_get_max_number_of_pages(atc, base + 10, page_size * 20) == 21);
    g_assert(atc_get_max_number_of_pages(atc, base + 10,
                                         (page_size * 20) + (page_size - 10))
                                          == 21);
    g_assert(atc_get_max_number_of_pages(atc, base + 10,
                                         (page_size * 20) +
                                         (page_size - 10 + 1)) == 22);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/atc/test_creation_parameters", test_creation_parameters);
    g_test_add_func("/atc/test_single_entry", test_single_entry);
    g_test_add_func("/atc/test_single_entry_2", test_single_entry_2);
    g_test_add_func("/atc/test_page_boundaries", test_page_boundaries);
    g_test_add_func("/atc/test_huge_page", test_huge_page);
    g_test_add_func("/atc/test_pasid", test_pasid);
    g_test_add_func("/atc/test_large_address", test_large_address);
    g_test_add_func("/atc/test_bigger_page", test_bigger_page);
    g_test_add_func("/atc/test_unknown_pasid", test_unknown_pasid);
    g_test_add_func("/atc/test_invalidation", test_invalidation);
    g_test_add_func("/atc/test_delete_address_space_cache",
                    test_delete_address_space_cache);
    g_test_add_func("/atc/test_invalidate_entire_address_space",
                    test_invalidate_entire_address_space);
    g_test_add_func("/atc/test_reset", test_reset);
    g_test_add_func("/atc/test_get_max_number_of_pages",
                    test_get_max_number_of_pages);
    return g_test_run();
}
