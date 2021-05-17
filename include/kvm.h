/**
 * libivee internal KVM api
 */

#pragma once

struct ivee_memory_map;

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
