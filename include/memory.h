#pragma once

#include <inttypes.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <sys/queue.h>

/* We assume 64-bit VMs */
typedef uint64_t gpa_t;
#define IVEE_GPA_LAST UINT64_MAX

/**
 * Host memory region.
 * Can be mapped to specific GPA ranges.
 */
struct ivee_host_memory_region
{
    /* Host virtual address */
    void* hva;

    /* Region length in bytes */
    size_t length;

    /* Reference count */
    atomic_ulong refcount;
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

    /* Guest can't write to this region */
    bool ro;

    /* What do we have mapped here */
    struct ivee_host_memory_region* host;
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
 * Map host memory to create a memory region.
 * 
 * Mapped region will have refcount set to 1 with initial reference belonging to the caller.
 * Each guest mapping of the region will add another refcount.
 * Caller can drop its reference with ivee_drop_memory_region.
 *
 * \length      Length of the region in bytes.
 * \ro          Map host memory as readonly.
 *              Only affect what our process context can do with the memory, not what guest can.
 * \mmap_fd     Optional argument to specify what fd to use for an mmap call.
 *              If -1 then anonymous memory mapping will be created.
 * \out_mr      Memory region to initialize on success
 */
int ivee_map_host_memory(size_t length, bool ro, int mmap_fd, struct ivee_host_memory_region* out_mr);

/**
 * Drop owner reference to host memory region.
 * Once no guest mappings will exist for this region it will be complete released.
 */
void ivee_drop_host_memory(struct ivee_host_memory_region* r);

/**
 * Map host memory region into guest physical address space.
 *
 * Single host region can be mapped any number of times to different guest regions.
 * No region overlaps are allowed in memory map.
 *
 * \memmap      Flat memory map to make changes to
 * \host        Host memory region to map
 * \gpa         GPA where region will start
 * \ro          If true, guest will not be able to write into this particular mapping of this host memory.
 */
int ivee_map_guest_region(struct ivee_memory_map* memmap, struct ivee_host_memory_region* host, gpa_t gpa, bool ro);
