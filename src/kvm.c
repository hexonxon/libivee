#include <inttypes.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/kvm.h>

#include "libivee/libivee.h"
#include "platform.h"
#include "kvm.h"

#define MIN_KVM_VERSION 12

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
