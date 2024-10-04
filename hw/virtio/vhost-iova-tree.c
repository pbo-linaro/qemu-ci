/*
 * vhost software live migration iova tree
 *
 * SPDX-FileCopyrightText: Red Hat, Inc. 2021
 * SPDX-FileContributor: Author: Eugenio Pérez <eperezma@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/iova-tree.h"
#include "vhost-iova-tree.h"

#define iova_min_addr qemu_real_host_page_size()

/**
 * VhostIOVATree, able to:
 * - Translate iova address
 * - Reverse translate iova address (from translated to iova)
 * - Allocate IOVA regions for translated range (linear operation)
 */
struct VhostIOVATree {
    /* First addressable iova address in the device */
    uint64_t iova_first;

    /* Last addressable iova address in the device */
    uint64_t iova_last;

    /* IOVA address to qemu memory maps. */
    IOVATree *iova_taddr_map;

    /* IOVA address to guest memory maps. */
    IOVATree *iova_gpa_map;
};

/**
 * Create a new VhostIOVATree
 *
 * Returns the new VhostIOVATree
 */
VhostIOVATree *vhost_iova_tree_new(hwaddr iova_first, hwaddr iova_last)
{
    VhostIOVATree *tree = g_new(VhostIOVATree, 1);

    /* Some devices do not like 0 addresses */
    tree->iova_first = MAX(iova_first, iova_min_addr);
    tree->iova_last = iova_last;

    tree->iova_taddr_map = iova_tree_new();
    tree->iova_gpa_map = iova_tree_new();
    return tree;
}

/**
 * Delete an iova tree
 */
void vhost_iova_tree_delete(VhostIOVATree *iova_tree)
{
    iova_tree_destroy(iova_tree->iova_taddr_map);
    iova_tree_destroy(iova_tree->iova_gpa_map);
    g_free(iova_tree);
}

/**
 * Find the IOVA address stored from a memory address
 *
 * @tree: The iova tree
 * @map: The map with the memory address
 *
 * Return the stored mapping, or NULL if not found.
 */
const DMAMap *vhost_iova_tree_find_iova(const VhostIOVATree *tree,
                                        const DMAMap *map)
{
    return iova_tree_find_iova(tree->iova_taddr_map, map);
}

/**
 * Allocate a new mapping in the IOVA->HVA tree
 *
 * @tree: The iova tree
 * @map: The iova map
 *
 * Returns:
 * - IOVA_OK if the map fits in the container
 * - IOVA_ERR_INVALID if the map does not make sense (like size overflow)
 * - IOVA_ERR_NOMEM if tree cannot allocate more space.
 *
 * It returns assignated iova in map->iova if return value is VHOST_DMA_MAP_OK.
 */
int vhost_iova_tree_map_alloc(VhostIOVATree *tree, DMAMap *map)
{
    /* Some vhost devices do not like addr 0. Skip first page */
    hwaddr iova_first = tree->iova_first ?: qemu_real_host_page_size();

    if (map->translated_addr + map->size < map->translated_addr ||
        map->perm == IOMMU_NONE) {
        return IOVA_ERR_INVALID;
    }

    /* Allocate a node in IOVA address */
    return iova_tree_alloc_map(tree->iova_taddr_map, map, iova_first,
                               tree->iova_last);
}

/**
 * Remove existing mappings from iova tree
 *
 * @iova_tree: The vhost iova tree
 * @map: The map to remove
 */
void vhost_iova_tree_remove(VhostIOVATree *iova_tree, DMAMap map)
{
    iova_tree_remove(iova_tree->iova_taddr_map, map);
}

/**
 * Find the IOVA address stored from a guest memory address
 *
 * @tree: The VhostIOVATree
 * @map: The map with the guest memory address
 *
 * Return the stored mapping, or NULL if not found.
 */
const DMAMap *vhost_iova_gpa_tree_find_iova(const VhostIOVATree *tree,
                                            const DMAMap *map)
{
    return iova_tree_find_iova(tree->iova_gpa_map, map);
}

/**
 * Allocate new mappings in the IOVA->HVA & IOVA->GPA trees
 *
 * @tree: The VhostIOVATree
 * @map: The iova map
 * @gpa: The guest physical address (GPA)
 *
 * Returns:
 * - IOVA_OK if the map fits both containers
 * - IOVA_ERR_INVALID if the map does not make sense (like size overflow)
 * - IOVA_ERR_NOMEM if the IOVA->HVA tree cannot allocate more space
 *
 * It returns an assigned iova in map->iova if return value is IOVA_OK.
 */
int vhost_iova_tree_map_alloc_gpa(VhostIOVATree *tree, DMAMap *map, hwaddr gpa)
{
    int ret;

    /* Some vhost devices don't like addr 0. Skip first page */
    hwaddr iova_first = tree->iova_first ?: qemu_real_host_page_size();

    if (map->translated_addr + map->size < map->translated_addr ||
        map->perm == IOMMU_NONE) {
        return IOVA_ERR_INVALID;
    }

    /* Allocate a node in the IOVA->HVA tree */
    ret = iova_tree_alloc_map(tree->iova_taddr_map, map, iova_first,
                              tree->iova_last);
    if (unlikely(ret != IOVA_OK)) {
        return ret;
    }

    /* Insert a node in the IOVA->GPA tree */
    map->translated_addr = gpa;
    return iova_tree_insert(tree->iova_gpa_map, map);
}

/**
 * Remove existing mappings from the IOVA->HVA & IOVA->GPA trees
 *
 * @iova_tree: The VhostIOVATree
 * @map: The map to remove
 */
void vhost_iova_tree_remove_gpa(VhostIOVATree *iova_tree, DMAMap map)
{
    /* Remove the existing mapping from the IOVA->GPA tree */
    iova_tree_remove(iova_tree->iova_gpa_map, map);

    /* Remove the corresponding mapping from the IOVA->HVA tree */
    iova_tree_remove(iova_tree->iova_taddr_map, map);
}
