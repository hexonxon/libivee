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
