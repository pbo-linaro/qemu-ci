/*
 * CPU interfaces that are target independent.
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */
#ifndef CPU_COMMON_H
#define CPU_COMMON_H

#include "exec/vaddr.h"
#ifndef CONFIG_USER_ONLY
#include "exec/hwaddr.h"
#endif
#include "hw/core/cpu.h"
#include "tcg/debug-assert.h"
#include "exec/page-protection.h"

#define EXCP_INTERRUPT  0x10000 /* async interruption */
#define EXCP_HLT        0x10001 /* hlt instruction reached */
#define EXCP_DEBUG      0x10002 /* cpu stopped after a breakpoint or singlestep */
#define EXCP_HALTED     0x10003 /* cpu is halted (waiting for external event) */
#define EXCP_YIELD      0x10004 /* cpu wants to yield timeslice to another */
#define EXCP_ATOMIC     0x10005 /* stop-the-world and emulate atomic */

void cpu_exec_init_all(void);
void cpu_exec_step_atomic(CPUState *cpu);

#define REAL_HOST_PAGE_ALIGN(addr) ROUND_UP((addr), qemu_real_host_page_size())

/* The CPU list lock nests outside page_(un)lock or mmap_(un)lock */
extern QemuMutex qemu_cpu_list_lock;
void qemu_init_cpu_list(void);
void cpu_list_lock(void);
void cpu_list_unlock(void);
unsigned int cpu_list_generation_id_get(void);

int cpu_get_free_index(void);

void tcg_iommu_init_notifier_list(CPUState *cpu);
void tcg_iommu_free_notifier_list(CPUState *cpu);

#if !defined(CONFIG_USER_ONLY)

enum device_endian {
    DEVICE_NATIVE_ENDIAN,
    DEVICE_BIG_ENDIAN,
    DEVICE_LITTLE_ENDIAN,
};

#if HOST_BIG_ENDIAN
#define DEVICE_HOST_ENDIAN DEVICE_BIG_ENDIAN
#else
#define DEVICE_HOST_ENDIAN DEVICE_LITTLE_ENDIAN
#endif

#include "sysemu/ram_addr.h"

/**
 * cpu_address_space_init:
 * @cpu: CPU to add this address space to
 * @asidx: integer index of this address space
 * @prefix: prefix to be used as name of address space
 * @mr: the root memory region of address space
 *
 * Add the specified address space to the CPU's cpu_ases list.
 * The address space added with @asidx 0 is the one used for the
 * convenience pointer cpu->as.
 * The target-specific code which registers ASes is responsible
 * for defining what semantics address space 0, 1, 2, etc have.
 *
 * Before the first call to this function, the caller must set
 * cpu->num_ases to the total number of address spaces it needs
 * to support.
 *
 * Note that with KVM only one address space is supported.
 */
void cpu_address_space_init(CPUState *cpu, int asidx,
                            const char *prefix, MemoryRegion *mr);
/**
 * cpu_address_space_destroy:
 * @cpu: CPU for which address space needs to be destroyed
 * @asidx: integer index of this address space
 *
 * Note that with KVM only one address space is supported.
 */
void cpu_address_space_destroy(CPUState *cpu, int asidx);

void cpu_physical_memory_rw(hwaddr addr, void *buf,
                            hwaddr len, bool is_write);
static inline void cpu_physical_memory_read(hwaddr addr,
                                            void *buf, hwaddr len)
{
    cpu_physical_memory_rw(addr, buf, len, false);
}
static inline void cpu_physical_memory_write(hwaddr addr,
                                             const void *buf, hwaddr len)
{
    cpu_physical_memory_rw(addr, (void *)buf, len, true);
}
void *cpu_physical_memory_map(hwaddr addr,
                              hwaddr *plen,
                              bool is_write);
void cpu_physical_memory_unmap(void *buffer, hwaddr len,
                               bool is_write, hwaddr access_len);

bool cpu_physical_memory_is_io(hwaddr phys_addr);

/* Coalesced MMIO regions are areas where write operations can be reordered.
 * This usually implies that write operations are side-effect free.  This allows
 * batching which can make a major impact on performance when using
 * virtualization.
 */
void qemu_flush_coalesced_mmio_buffer(void);

void cpu_flush_icache_range(hwaddr start, hwaddr len);

typedef int (RAMBlockIterFunc)(RAMBlock *rb, void *opaque);

int qemu_ram_foreach_block(RAMBlockIterFunc func, void *opaque);
int ram_block_discard_range(RAMBlock *rb, uint64_t start, size_t length);
int ram_block_discard_guest_memfd_range(RAMBlock *rb, uint64_t start,
                                        size_t length);

#endif

/* Returns: 0 on success, -1 on error */
int cpu_memory_rw_debug(CPUState *cpu, vaddr addr,
                        void *ptr, size_t len, bool is_write);

/* vl.c */
void list_cpus(void);

#ifdef CONFIG_TCG

bool tcg_cflags_has(CPUState *cpu, uint32_t flags);
void tcg_cflags_set(CPUState *cpu, uint32_t flags);

/* current cflags for hashing/comparison */
uint32_t curr_cflags(CPUState *cpu);

/**
 * cpu_unwind_state_data:
 * @cpu: the cpu context
 * @host_pc: the host pc within the translation
 * @data: output data
 *
 * Attempt to load the the unwind state for a host pc occurring in
 * translated code.  If @host_pc is not in translated code, the
 * function returns false; otherwise @data is loaded.
 * This is the same unwind info as given to restore_state_to_opc.
 */
bool cpu_unwind_state_data(CPUState *cpu, uintptr_t host_pc, uint64_t *data);

/**
 * cpu_restore_state:
 * @cpu: the cpu context
 * @host_pc: the host pc within the translation
 * @return: true if state was restored, false otherwise
 *
 * Attempt to restore the state for a fault occurring in translated
 * code. If @host_pc is not in translated code no state is
 * restored and the function returns false.
 */
bool cpu_restore_state(CPUState *cpu, uintptr_t host_pc);

G_NORETURN void cpu_loop_exit_noexc(CPUState *cpu);
G_NORETURN void cpu_loop_exit_atomic(CPUState *cpu, uintptr_t pc);
#endif /* CONFIG_TCG */
G_NORETURN void cpu_loop_exit(CPUState *cpu);
G_NORETURN void cpu_loop_exit_restore(CPUState *cpu, uintptr_t pc);

/* accel/tcg/cpu-exec.c */
int cpu_exec(CPUState *cpu);

/**
 * env_archcpu(env)
 * @env: The architecture environment
 *
 * Return the ArchCPU associated with the environment.
 */
static inline ArchCPU *env_archcpu(CPUArchState *env)
{
    return (void *)env - sizeof(CPUState);
}

/**
 * env_cpu_const(env)
 * @env: The architecture environment
 *
 * Return the CPUState associated with the environment.
 */
static inline const CPUState *env_cpu_const(const CPUArchState *env)
{
    return (void *)env - sizeof(CPUState);
}

/**
 * env_cpu(env)
 * @env: The architecture environment
 *
 * Return the CPUState associated with the environment.
 */
static inline CPUState *env_cpu(CPUArchState *env)
{
    return (CPUState *)env_cpu_const(env);
}

#ifndef CONFIG_USER_ONLY
/**
 * cpu_mmu_index:
 * @env: The cpu environment
 * @ifetch: True for code access, false for data access.
 *
 * Return the core mmu index for the current translation regime.
 * This function is used by generic TCG code paths.
 *
 * The user-only version of this function is inline in cpu-all.h,
 * where it always returns MMU_USER_IDX.
 */
static inline int cpu_mmu_index(CPUState *cs, bool ifetch)
{
    int ret = cs->cc->mmu_index(cs, ifetch);
    tcg_debug_assert(ret >= 0 && ret < NB_MMU_MODES);
    return ret;
}
#endif /* !CONFIG_USER_ONLY */

#endif /* CPU_COMMON_H */
