#include <inttypes.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/kvm.h>

#include "libivee/libivee.h"
#include "platform.h"
#include "memory.h"
#include "x86.h"
#include "kvm.h"

#define MIN_KVM_VERSION 12
#define MAX_KVM_MEMORY_SLOTS 16

/**
 * KVM memory slot tracking
 */
struct ivee_kvm_memory_slot
{
    /* KVM slot index */
    int index;

    /* Is slot in use? */
    bool is_used;

    /* Is slot readonly? */
    bool is_ro;

    /* GPA start */
    gpa_t first_gpa;

    /* GPA end */
    gpa_t last_gpa;

    /* Mapped host virtual address */
    uintptr_t hva;
};

/**
 * KVM VM context
 */
struct ivee_kvm_vm
{
    /* KVM VM fd */
    int fd;

    /* KVM VCPU fd */
    int vcpu_fd;

    /* Size of mapped KVM vcpu data region */
    size_t vcpu_mapping_size;

    /* Mapped KVM vcpu data */
    struct kvm_run* kvm_run;

    /* Memory slot array */
    struct ivee_kvm_memory_slot memory_slots[MAX_KVM_MEMORY_SLOTS];
};

static struct ivee_kvm_info {
    int devfd;
} g_kvm = {
    .devfd = -1,
};

static int kvm_ioctl(int fd, unsigned long request, uintptr_t arg)
{
    int ret = ioctl(fd, request, arg);
    if (ret < 0) {
        return -errno;
    }

    return ret;
}

static inline int kvm_ioctl_noargs(int fd, unsigned long request)
{
    return kvm_ioctl(fd, request, 0);
}

int ivee_init_kvm(void)
{
    int res = 0;

    /* Already initialized */
    if (g_kvm.devfd >= 0) {
        return 0;
    }

    res = open("/dev/kvm", O_RDONLY);
    if (res < 0) {
        return res;
    }

    g_kvm.devfd = res;

    res = kvm_ioctl_noargs(g_kvm.devfd, KVM_GET_API_VERSION);
    if (res < 0) {
        return res;
    }

    if (res < MIN_KVM_VERSION) {
        return -ENOTSUP;
    }

    res = kvm_ioctl(g_kvm.devfd, KVM_CHECK_EXTENSION, KVM_CAP_NR_VCPUS);
    if (res < 0) {
        return res;
    } else if (res < 1) {
        /* Sanity-check that KVM can handle 1 vcpu vms */
        return -ENOTSUP;
    }

    res = kvm_ioctl(g_kvm.devfd, KVM_CHECK_EXTENSION, KVM_CAP_NR_MEMSLOTS);
    if (res < 0) {
        return res;
    }

    if (res < MAX_KVM_MEMORY_SLOTS) {
        return -ENOSPC;
    }

    return 0;
}

struct ivee_kvm_vm* ivee_create_kvm_vm(void)
{
    struct ivee_kvm_vm* vm = ivee_zalloc(sizeof(*vm));
    if (!vm) {
        return NULL;
    }

    vm->fd = -1;
    vm->vcpu_fd = -1;

    vm->fd = kvm_ioctl(g_kvm.devfd, KVM_CREATE_VM, 0);
    if (vm->fd < 0) {
        goto error_out;
    }

    vm->vcpu_fd = kvm_ioctl(vm->fd, KVM_CREATE_VCPU, IVEE_VCPU_APIC_ID);
    if (vm->vcpu_fd < 0) {
        goto error_out;
    }

    vm->vcpu_mapping_size = kvm_ioctl_noargs(g_kvm.devfd, KVM_GET_VCPU_MMAP_SIZE);
    if (vm->vcpu_mapping_size < 0) {
        goto error_out;
    }

    vm->kvm_run = mmap(NULL, vm->vcpu_mapping_size, PROT_READ|PROT_WRITE, MAP_SHARED, vm->vcpu_fd, 0);
    if (vm->kvm_run == MAP_FAILED) {
        goto error_out;
    }

    for (size_t i = 0; i < MAX_KVM_MEMORY_SLOTS; ++i) {
        struct ivee_kvm_memory_slot* slot = vm->memory_slots + i;
        slot->index = i;
    }

    return vm;

error_out:
    ivee_release_kvm_vm(vm);
    return NULL;
}

void ivee_release_kvm_vm(struct ivee_kvm_vm* vm)
{
    if (!vm) {
        return;
    }

    /* We should handle partially-initialized vms here */

    if (vm->kvm_run != NULL) {
        munmap(vm->kvm_run, vm->vcpu_mapping_size);
    }

    if (vm->vcpu_fd >= 0) {
        close(vm->vcpu_fd);
    }

    if (vm->fd >= 0) {
        close(vm->fd);
    }

    ivee_free(vm);
}

static int set_memory_slot(struct ivee_kvm_vm* vm, struct ivee_kvm_memory_slot* slot)
{
    struct kvm_userspace_memory_region memregion;
    memregion.slot = slot->index;
    memregion.flags = slot->is_ro ? KVM_MEM_READONLY : 0;
    memregion.guest_phys_addr = slot->first_gpa;
    memregion.memory_size = slot->last_gpa - slot->first_gpa + 1;
    memregion.userspace_addr = slot->hva;

    return kvm_ioctl(vm->fd, KVM_SET_USER_MEMORY_REGION, (uintptr_t)&memregion);
}

static int delete_memory_slot(struct ivee_kvm_vm* vm, struct ivee_kvm_memory_slot* slot)
{
    struct kvm_userspace_memory_region memregion;
    memregion.slot = slot->index;
    memregion.memory_size = 0;

    return kvm_ioctl(vm->fd, KVM_SET_USER_MEMORY_REGION, (uintptr_t)&memregion);
}

int ivee_set_kvm_memory_map(struct ivee_kvm_vm* vm, const struct ivee_memory_map* memmap)
{
    if (!vm || !memmap) {
        return -EINVAL;
    }

    /*
     * Blindly resetting slot contents is expensive in KVM, however we don't expect memory map to change
     * often (or at all) for our cases, so we will just reconstruct slots entirely from each new memory map.
     *
     * If frequency of memmap updates ever changes this could become a problem.
     */

    for (size_t i = 0; i < MAX_KVM_MEMORY_SLOTS; ++i) {
        struct ivee_kvm_memory_slot* slot = vm->memory_slots + i;
        if (!slot->is_used) {
            continue;
        }

        int res = delete_memory_slot(vm, slot);
        if (res != 0) {
            return res;
        }

        slot->is_used = false;
    }

    size_t index = 0;
    struct ivee_guest_memory_region* r;
    LIST_FOREACH(r, &memmap->regions, link) {
        if (index == MAX_KVM_MEMORY_SLOTS) {
            return -ENOSPC;
        }

        struct ivee_kvm_memory_slot* slot = vm->memory_slots + index;
        slot->first_gpa = r->first_gfn << 12;
        slot->last_gpa = ((r->last_gfn + 1) << 12) - 1;
        slot->is_ro = r->ro;
        slot->hva = (uintptr_t)(r->host ? r->host->hva : NULL);

        int res = set_memory_slot(vm, slot);
        if (res != 0) {
            return res;
        }

        slot->is_used = true;
        ++index;
    }

    return 0;
}
