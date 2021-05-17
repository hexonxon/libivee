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

static void load_segment(struct kvm_segment* kvmseg, const struct x86_segment* seg)
{
    kvmseg->base = seg->base;
    kvmseg->limit = seg->limit;
    kvmseg->selector = seg->selector;
    kvmseg->type = seg->type;
    kvmseg->dpl = seg->dpl;
    kvmseg->present = !!(seg->flags & X86_SEG_P);
    kvmseg->db = !!(seg->flags & X86_SEG_DB);
    kvmseg->s = !!(seg->flags & X86_SEG_S);
    kvmseg->l = !!(seg->flags & X86_SEG_L);
    kvmseg->g = !!(seg->flags & X86_SEG_G);
    kvmseg->avl = !!(seg->flags & X86_SEG_AVL);
    kvmseg->unusable = !kvmseg->present;
}

static void store_segment(const struct kvm_segment* kvmseg, struct x86_segment* seg)
{
    seg->base = kvmseg->base;
    seg->limit = kvmseg->limit;
    seg->selector = kvmseg->selector;
    seg->type = kvmseg->type;
    seg->dpl = kvmseg->dpl;
    seg->flags |= (kvmseg->present ? X86_SEG_P : 0);
    seg->flags |= (kvmseg->db ? X86_SEG_DB : 0);
    seg->flags |= (kvmseg->s ? X86_SEG_S : 0);
    seg->flags |= (kvmseg->l ? X86_SEG_L : 0);
    seg->flags |= (kvmseg->g ? X86_SEG_G : 0);
    seg->flags |= (kvmseg->avl ? X86_SEG_AVL : 0);
}

static void load_dtable(struct kvm_dtable* kvm_dtable, const struct x86_dtbl* dtable)
{
    kvm_dtable->base = dtable->base;
    kvm_dtable->limit = dtable->limit;
    memset(kvm_dtable->padding, 0, sizeof(kvm_dtable->padding));
}

/* Load effective cpu state into KVM vcpu */
static int load_vcpu_state(struct ivee_kvm_vm* vm, const struct x86_cpu_state* x86_cpu)
{
    int res = 0;

    struct kvm_regs kvm_regs;
    kvm_regs.rax = x86_cpu->rax;
    kvm_regs.rbx = x86_cpu->rbx;
    kvm_regs.rcx = x86_cpu->rcx;
    kvm_regs.rdx = x86_cpu->rdx;
    kvm_regs.rsi = x86_cpu->rsi;
    kvm_regs.rdi = x86_cpu->rdi;
    kvm_regs.rsp = x86_cpu->rsp;
    kvm_regs.rbp = x86_cpu->rbp;
    kvm_regs.r8 = x86_cpu->r8;
    kvm_regs.r9 = x86_cpu->r9;
    kvm_regs.r10 = x86_cpu->r10;
    kvm_regs.r11 = x86_cpu->r11;
    kvm_regs.r12 = x86_cpu->r12;
    kvm_regs.r13 = x86_cpu->r13;
    kvm_regs.r14 = x86_cpu->r14;
    kvm_regs.r15 = x86_cpu->r15;
    kvm_regs.rip = x86_cpu->rip;
    kvm_regs.rflags = x86_cpu->rflags;

    res = kvm_ioctl(vm->vcpu_fd, KVM_SET_REGS, (uintptr_t)&kvm_regs);
    if (res != 0) {
        return res;
    }

    struct kvm_sregs kvm_sregs = {0};
    load_segment(&kvm_sregs.cs, &x86_cpu->cs);
    load_segment(&kvm_sregs.ds, &x86_cpu->ds);
    load_segment(&kvm_sregs.es, &x86_cpu->es);
    load_segment(&kvm_sregs.fs, &x86_cpu->fs);
    load_segment(&kvm_sregs.gs, &x86_cpu->gs);
    load_segment(&kvm_sregs.ss, &x86_cpu->ss);
    load_segment(&kvm_sregs.tr, &x86_cpu->tr);
    load_segment(&kvm_sregs.ldt, &x86_cpu->ldt);
    load_dtable(&kvm_sregs.gdt, &x86_cpu->gdt);
    load_dtable(&kvm_sregs.idt, &x86_cpu->idt);
    kvm_sregs.cr0 = x86_cpu->cr0;
    kvm_sregs.cr2 = x86_cpu->cr2;
    kvm_sregs.cr3 = x86_cpu->cr3;
    kvm_sregs.cr4 = x86_cpu->cr4;
    kvm_sregs.efer = x86_cpu->efer;
    kvm_sregs.apic_base = x86_cpu->apic_base;

    res = kvm_ioctl(vm->vcpu_fd, KVM_SET_SREGS, (uintptr_t)&kvm_sregs);
    if (res != 0) {
        return res;
    }

    return 0;
}

/* Store effective cpu state from KVM vcpu */
__attribute__((unused)) static int store_vcpu_state(struct ivee_kvm_vm* vm, struct x86_cpu_state* x86_cpu)
{
    int res = 0;

    struct kvm_regs kvm_regs;
    res = kvm_ioctl(vm->vcpu_fd, KVM_GET_REGS, (uintptr_t)&kvm_regs);
    if (res != 0) {
        return res;
    }

    x86_cpu->rax = kvm_regs.rax;
    x86_cpu->rbx = kvm_regs.rbx;
    x86_cpu->rcx = kvm_regs.rcx;
    x86_cpu->rdx = kvm_regs.rdx;
    x86_cpu->rsi = kvm_regs.rsi;
    x86_cpu->rdi = kvm_regs.rdi;
    x86_cpu->rsp = kvm_regs.rsp;
    x86_cpu->rbp = kvm_regs.rbp;
    x86_cpu->r8 = kvm_regs.r8;
    x86_cpu->r9 = kvm_regs.r9;
    x86_cpu->r10 = kvm_regs.r10;
    x86_cpu->r11 = kvm_regs.r11;
    x86_cpu->r12 = kvm_regs.r12;
    x86_cpu->r13 = kvm_regs.r13;
    x86_cpu->r14 = kvm_regs.r14;
    x86_cpu->r15 = kvm_regs.r15;
    x86_cpu->rip = kvm_regs.rip;
    x86_cpu->rflags = kvm_regs.rflags;

    struct kvm_sregs kvm_sregs = {0};
    res = kvm_ioctl(vm->vcpu_fd, KVM_GET_SREGS, (uintptr_t)&kvm_sregs);
    if (res != 0) {
        return res;
    }

    store_segment(&kvm_sregs.cs, &x86_cpu->cs);
    store_segment(&kvm_sregs.ds, &x86_cpu->ds);
    store_segment(&kvm_sregs.es, &x86_cpu->es);
    store_segment(&kvm_sregs.fs, &x86_cpu->fs);
    store_segment(&kvm_sregs.gs, &x86_cpu->gs);
    store_segment(&kvm_sregs.ss, &x86_cpu->ss);
    store_segment(&kvm_sregs.tr, &x86_cpu->tr);
    store_segment(&kvm_sregs.ldt, &x86_cpu->ldt);
    x86_cpu->cr0 = kvm_sregs.cr0;
    x86_cpu->cr2 = kvm_sregs.cr2;
    x86_cpu->cr3 = kvm_sregs.cr3;
    x86_cpu->cr4 = kvm_sregs.cr4;
    x86_cpu->efer = kvm_sregs.efer;

    return 0;
}

int ivee_kvm_load_vcpu_state(struct ivee_kvm_vm* vm, struct x86_cpu_state* x86_cpu)
{
    return load_vcpu_state(vm, x86_cpu);
}

int ivee_kvm_store_vcpu_state(struct ivee_kvm_vm* vm, struct x86_cpu_state* x86_cpu)
{
    return store_vcpu_state(vm, x86_cpu);
}

int ivee_kvm_run(struct ivee_kvm_vm* vm, struct ivee_exit* exit)
{
    int res = 0;

    res = kvm_ioctl_noargs(vm->vcpu_fd, KVM_RUN);
    if (res != 0) {
        return res;
    }

    switch (vm->kvm_run->exit_reason) {
    case KVM_EXIT_IO:
        exit->exit_reason = IVEE_EXIT_IO;
        exit->io.port = vm->kvm_run->io.port;
        exit->io.op = vm->kvm_run->io.direction;
        exit->io.size = vm->kvm_run->io.size;
        exit->io.data = 0;
        memcpy(&exit->io.data, (uint8_t*)vm->kvm_run + vm->kvm_run->io.data_offset, vm->kvm_run->io.size);

        return 0;

    default:
        exit->exit_reason = IVEE_EXIT_UNKNOWN;
        return 0;
    };
}
