#pragma once

#include <inttypes.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <sys/queue.h>

/* We assume 64-bit VMs */
typedef uint64_t gpa_t;
#define IVEE_GPA_LAST UINT64_MAX

/**
 * Your typical RWX memory protection flags
 */
enum ivee_memory_prot
{
    IVEE_READ   = (1u << 0),
    IVEE_WRITE  = (1u << 1),
    IVEE_EXEC   = (1u << 2),
};

/**
 * Guest physical memory region.
 * Defines a GPA range and keeps track of what is mapped there.
 */
struct ivee_guest_memory_region
{
    /* Link to list of guest regions for containing VM */
    LIST_ENTRY(ivee_guest_memory_region) link;

    /* Region GFN range */
    gpa_t first_gfn;
    gpa_t last_gfn;

    /* Host virtual address of memory mapped at this guest region */
    void* hva;

    /* Region length in bytes */
    size_t length;

    /* Guest memory protection bits */
    enum ivee_memory_prot prot;
};

/**
 * VM memory map definition.
 *
 * A flat memory map represents a list of guest physical address space regions
 * which may be backed by host virtual address regions. Unmapped regions trigger EPT faults
 * and exit into monitor (us).
 *
 * This structure is produced by higher-level vm management code and consumed
 * by hypervisor implementations (e.g. KVM) to turn into something hardware can understand.
 */
struct ivee_memory_map
{
    /* List of mapped guest memory regions */
    LIST_HEAD(, ivee_guest_memory_region) regions;
};

/**
 * Init fresh memory map with no regions
 */
int ivee_init_memory_map(struct ivee_memory_map* map);

/**
 * Free all guest regions in this memory map
 */
void ivee_free_memory_map(struct ivee_memory_map* map);

/**
 * Allocate a block of host memory and map it into the guest memory map at specified GPA.
 *
 * \map         Flat memory map to make changes to
 * \gpa         GPA where region will start
 * \length      Length of the region in bytes, will be rounded up to guest page size
 *              Only affect what our process context can do with the memory, not what guest can
 * \mmap_fd     Optional argument to specify what fd to use for an mmap call
 *              If -1 then anonymous memory mapping will be created.
 * \host_ro     Host memory is mapped as PROT_READ instead of default PROT_READ|PROT_WRITE
 *              This does not affect guest access permissions (see \prot argument for that)
 * \prot        Guest access permissions
 *
 * Returns newly allocate guest memory region on success, stored in memory map.
 */
struct ivee_guest_memory_region* ivee_map_host_memory(struct ivee_memory_map* map,
                                                      gpa_t gpa,
                                                      size_t length,
                                                      int mmap_fd,
                                                      bool host_ro,
                                                      enum ivee_memory_prot prot);

/**
 * Unmap guest region and free associated host memory
 */
void ivee_unmap_host_memory(struct ivee_guest_memory_region* mr);
