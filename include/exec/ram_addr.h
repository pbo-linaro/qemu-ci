/*
 * Declarations for cpu physical memory functions
 *
 * Copyright 2011 Red Hat, Inc. and/or its affiliates
 *
 * Authors:
 *  Avi Kivity <avi@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 *
 */

/*
 * This header is for use by exec.c and memory.c ONLY.  Do not include it.
 * The functions declared here will be removed soon.
 */

#ifndef RAM_ADDR_H
#define RAM_ADDR_H

#ifndef CONFIG_USER_ONLY

/* address in the RAM (different from a physical address) */
#if defined(CONFIG_XEN_BACKEND)
typedef uint64_t ram_addr_t;
#  define RAM_ADDR_MAX UINT64_MAX
#  define RAM_ADDR_FMT "%" PRIx64
#else
typedef uintptr_t ram_addr_t;
#  define RAM_ADDR_MAX UINTPTR_MAX
#  define RAM_ADDR_FMT "%" PRIxPTR
#endif

/* memory API */

void qemu_ram_remap(ram_addr_t addr, ram_addr_t length);
/* This should not be used by devices.  */
ram_addr_t qemu_ram_addr_from_host(void *ptr);
ram_addr_t qemu_ram_addr_from_host_nofail(void *ptr);
RAMBlock *qemu_ram_block_by_name(const char *name);

/*
 * Translates a host ptr back to a RAMBlock and an offset in that RAMBlock.
 *
 * @ptr: The host pointer to translate.
 * @round_offset: Whether to round the result offset down to a target page
 * @offset: Will be set to the offset within the returned RAMBlock.
 *
 * Returns: RAMBlock (or NULL if not found)
 *
 * By the time this function returns, the returned pointer is not protected
 * by RCU anymore.  If the caller is not within an RCU critical section and
 * does not hold the BQL, it must have other means of protecting the
 * pointer, such as a reference to the memory region that owns the RAMBlock.
 */
RAMBlock *qemu_ram_block_from_host(void *ptr, bool round_offset,
                                   ram_addr_t *offset);
ram_addr_t qemu_ram_block_host_offset(RAMBlock *rb, void *host);
void qemu_ram_set_idstr(RAMBlock *block, const char *name, DeviceState *dev);
void qemu_ram_unset_idstr(RAMBlock *block);
const char *qemu_ram_get_idstr(RAMBlock *rb);
void *qemu_ram_get_host_addr(RAMBlock *rb);
ram_addr_t qemu_ram_get_offset(RAMBlock *rb);
ram_addr_t qemu_ram_get_used_length(RAMBlock *rb);
ram_addr_t qemu_ram_get_max_length(RAMBlock *rb);
bool qemu_ram_is_shared(RAMBlock *rb);
bool qemu_ram_is_noreserve(RAMBlock *rb);
bool qemu_ram_is_uf_zeroable(RAMBlock *rb);
void qemu_ram_set_uf_zeroable(RAMBlock *rb);
bool qemu_ram_is_migratable(RAMBlock *rb);
void qemu_ram_set_migratable(RAMBlock *rb);
void qemu_ram_unset_migratable(RAMBlock *rb);
bool qemu_ram_is_named_file(RAMBlock *rb);
int qemu_ram_get_fd(RAMBlock *rb);

size_t qemu_ram_pagesize(RAMBlock *block);
size_t qemu_ram_pagesize_largest(void);

bool ramblock_is_pmem(RAMBlock *rb);

long qemu_minrampagesize(void);
long qemu_maxrampagesize(void);

/**
 * qemu_ram_alloc_from_file,
 * qemu_ram_alloc_from_fd:  Allocate a ram block from the specified backing
 *                          file or device
 *
 * Parameters:
 *  @size: the size in bytes of the ram block
 *  @mr: the memory region where the ram block is
 *  @ram_flags: RamBlock flags. Supported flags: RAM_SHARED, RAM_PMEM,
 *              RAM_NORESERVE, RAM_PROTECTED, RAM_NAMED_FILE, RAM_READONLY,
 *              RAM_READONLY_FD, RAM_GUEST_MEMFD
 *  @mem_path or @fd: specify the backing file or device
 *  @offset: Offset into target file
 *  @errp: pointer to Error*, to store an error if it happens
 *
 * Return:
 *  On success, return a pointer to the ram block.
 *  On failure, return NULL.
 */
RAMBlock *qemu_ram_alloc_from_file(ram_addr_t size, MemoryRegion *mr,
                                   uint32_t ram_flags, const char *mem_path,
                                   off_t offset, Error **errp);
RAMBlock *qemu_ram_alloc_from_fd(ram_addr_t size, MemoryRegion *mr,
                                 uint32_t ram_flags, int fd, off_t offset,
                                 Error **errp);

RAMBlock *qemu_ram_alloc_from_ptr(ram_addr_t size, void *host,
                                  MemoryRegion *mr, Error **errp);
RAMBlock *qemu_ram_alloc(ram_addr_t size, uint32_t ram_flags, MemoryRegion *mr,
                         Error **errp);
RAMBlock *qemu_ram_alloc_resizeable(ram_addr_t size, ram_addr_t max_size,
                                    void (*resized)(const char*,
                                                    uint64_t length,
                                                    void *host),
                                    MemoryRegion *mr, Error **errp);
void qemu_ram_free(RAMBlock *block);

int qemu_ram_resize(RAMBlock *block, ram_addr_t newsize, Error **errp);

void qemu_ram_msync(RAMBlock *block, ram_addr_t start, ram_addr_t length);

#endif /* CONFIG_USER_ONLY */

#endif
