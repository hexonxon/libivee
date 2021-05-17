#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include "libivee/libivee.h"
#include "platform.h"
#include "memory.h"
#include "x86.h"
#include "kvm.h"

struct ivee {
    /* Underlying KVM VM/VCPU */
    struct ivee_kvm_vm* vm;

    /* Active memory map */
    struct ivee_memory_map memory_map;

    /* x86 boot processor state */
    struct x86_cpu_state x86_cpu;

    /* Guest page tables memory region */
    struct ivee_host_memory_region gpt_mr;

    /* Mapped executable image memory region */
    struct ivee_host_memory_region image_mr;
};

uint64_t ivee_list_platform_capabilities(void)
{
    /* TODO: we don't support any caps yet */
    return 0;
}

int ivee_create(enum ivee_capabilities caps, struct ivee** out_ivee_ptr)
{
    if (!out_ivee_ptr) {
        return -EINVAL;
    }

    if (caps & ~ivee_list_platform_capabilities()) {
        return -ENOTSUP;
    }

    int res = 0;

    struct ivee* ivee = ivee_zalloc(sizeof(*ivee));
    if (!ivee) {
        return -ENOMEM;
    }

    res = ivee_init_kvm();
    if (res != 0) {
        goto error_out;
    }

    ivee->vm = ivee_create_kvm_vm();
    if (!ivee->vm) {
        res = -ENXIO;
        goto error_out;
    }

    res = ivee_init_memory_map(&ivee->memory_map);
    if (res != 0) {
        goto error_out;
    }

    *out_ivee_ptr = ivee;
    return 0;

error_out:
    ivee_destroy(ivee);
    return res;
}

void ivee_destroy(struct ivee* ivee)
{
    if (!ivee) {
        return;
    }

    ivee_release_kvm_vm(ivee->vm);
    ivee_free(ivee);
}

/*
 * We need the following amount of 4KiB guest page table pages to map 1GiB of memory in 4KiB pages:
 * 1 for PML4 + 1 for PDPE + 1 for PDE + 512 for PTEs = 515 pages
 *
 * Statically compute guest GPA for PML4 base address if we map it at the end of 4GiB address space.
 */
#define IVEE_PAGE_TABLE_SIZE    (0x1000ull * 515)
#define IVEE_PML4_BASE_GPA      (0x100000000ull - IVEE_PAGE_TABLE_SIZE)
#define IVEE_PDPE_BASE_GPA      (IVEE_PML4_BASE_GPA + 0x1000)
#define IVEE_PDE_BASE_GPA       (IVEE_PDPE_BASE_GPA + 0x1000)
#define IVEE_PTE_BASE_GPA       (IVEE_PDE_BASE_GPA + 0x1000)

/*
 * Setup guest identity-mapped 4KB page tables mapping first 1GiB of memory.
 * We will map it into guest memory directly.
 */
static int init_guest_page_table(struct ivee* ivee)
{
    int res = 0;

    res = ivee_map_host_memory(IVEE_PAGE_TABLE_SIZE, false, -1, &ivee->gpt_mr);
    if (res != 0) {
        return res;
    }

    uint64_t* pentry = (uint64_t*) ivee->gpt_mr.hva;

    /* 1 entry in PML4 */
    *pentry = IVEE_PDPE_BASE_GPA | 0x3;
    pentry += PAGE_SIZE / sizeof(*pentry);

    /* 1 entry in PDPE */
    *pentry = IVEE_PDE_BASE_GPA | 0x1;
    pentry += PAGE_SIZE / sizeof(*pentry);

    /* 512 entries in PDE */
    for (size_t i = 0; i < 512; ++i, ++pentry) {
        *pentry = (IVEE_PTE_BASE_GPA + PAGE_SIZE * i) | 0x3;
    }

    /* 256KiB entries in PTEs */
    for (size_t i = 0; i < (1 << 18); ++i, ++pentry) {
        *pentry = (PAGE_SIZE * i) | 0x3;
    }

    res = ivee_map_guest_region(&ivee->memory_map, &ivee->gpt_mr, IVEE_PML4_BASE_GPA, true);
    ivee_drop_host_memory(&ivee->gpt_mr);
    return res;
}

/* Load flat binary into VM and create a page table for it */
static int load_bin(struct ivee* ivee, const char* file)
{
    int res = 0;

    struct stat st;
    res = stat(file, &st);
    if (res != 0) {
        return res;
    }

    size_t size = st.st_size;
    if (size == 0) {
        return -EINVAL;
    }

    /*
     * Memory map the binary and map that into guest directly for readonly.
     * No other memory is mapped.
     */

    int fd = open(file, O_RDONLY);
    if (fd < 0) {
        return fd;
    }

    res = ivee_map_host_memory(size, true, fd, &ivee->image_mr);
    close(fd);

    if (res != 0) {
        goto error_out;
    }

    res = ivee_map_guest_region(&ivee->memory_map, &ivee->image_mr, 0, true);
    if (res != 0) {
        goto error_out;
    }

    ivee_drop_host_memory(&ivee->image_mr);
    return 0;

error_out:
    ivee_drop_host_memory(&ivee->image_mr);
    return res;
}

int ivee_load_executable(struct ivee* ivee, const char* file, ivee_executable_format_t format)
{
    int res = 0;

    if (!ivee) {
        return -EINVAL;
    }

    if (!file) {
        return -EINVAL;
    }

    /* We must have read and execute access for the file */
    if (0 != access(file, R_OK | X_OK)) {
        return -EINVAL;
    }

    switch (format) {
    case IVEE_EXEC_BIN:
        res = load_bin(ivee, file);
        break;
    default:
        res = -ENOTSUP;
    };

    if (res != 0) {
        return res;
    }

    res = init_guest_page_table(ivee);
    if (res != 0) {
        return res;
    }

    res = ivee_set_kvm_memory_map(ivee->vm, &ivee->memory_map);
    if (res != 0) {
        return res;
    }

    return res;
}
