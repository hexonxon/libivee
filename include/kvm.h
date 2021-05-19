/**
 * libivee internal KVM api
 */

#pragma once

struct ivee_memory_map;
struct x86_cpu_state;

/* TODO: this will eventually have to move to runtime interface header */
#define IVEE_PIO_EXIT_PORT 0x78u

/**
 * Valid ivee exit reasons we care about
 */
enum ivee_exit_reason {
    /** PIO is used to trap guest call returns */
    IVEE_EXIT_IO = 0,

    /** All other exit reasons are unexpected and unhandled */
    IVEE_EXIT_UNKNOWN,
};

/**
 * Port IO exit data
 */
struct ivee_pio_exit {
    uint32_t data;
    uint16_t port;
    uint8_t size;
    uint8_t op; /* 0 = read, 1 = write */
};

/**
 * VM exit information
 */
struct ivee_exit {
    enum ivee_exit_reason exit_reason;
    union {
        struct ivee_pio_exit io;
    };
};

/**
 * Opaque KVM VM container
 */
struct ivee_kvm_vm;

/**
 * Init static KVM context
 *
 * \returns     0 on success, negative value on error
 */
int ivee_init_kvm(void);

/**
 * Create libivee kvm vm container with 1 vcpu
 */
struct ivee_kvm_vm* ivee_create_kvm_vm(void);

/**
 * Release libivee kvm vm container
 */
void ivee_release_kvm_vm(struct ivee_kvm_vm* vm);

/**
 * Take ivee vm flat memory map and turn it into kvm memory slots.
 *
 * \vm      KVM VM instance
 * \memmap  High level memory map.
 *          Guaranteed to not have overlaps and to have all adjacent regions merged.
 */
int ivee_set_kvm_memory_map(struct ivee_kvm_vm* vm, const struct ivee_memory_map* memmap);

/**
 * Load x86 cpu state into KVM vcpu
 */
int ivee_kvm_load_vcpu_state(struct ivee_kvm_vm* vm, struct x86_cpu_state* x86_cpu);

/**
 * Get KVM vcpu state and store it in output x86 state
 */
int ivee_kvm_store_vcpu_state(struct ivee_kvm_vm* vm, struct x86_cpu_state* x86_cpu);

/**
 * Resume/start execution of KVM vcpu until next supported vmexit is initiated by the guest
 */
int ivee_kvm_run(struct ivee_kvm_vm* vm, struct ivee_exit* exit_reason);
