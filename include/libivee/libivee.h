#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdlib.h>

/**
 * APIC ID of a VCPU running inside an execution environment
 */
#define IVEE_VCPU_APIC_ID 0

/**
 * Host platform capabilities for execution environments.
 * We _require_ supported hypervisor to be present, thus it is not listed as a capability.
 */
typedef enum ivee_capabilities {
    /**
     * Platform is capable to provide manual management of environments' page faults.
     */
    IVEE_CAP_PAGE_FAULT_HANDLING = 0x0001,

    /**
     * Platform is capable to transparently encrypt memory allocated for an environment
     * with a unique encryption key not available to hypervisor or VMM.
     */
    IVEE_CAP_MEMORY_ENCRYPTION = 0x0002,
} ivee_capabilities_t;

/**
 * Supported executable file formats
 */
typedef enum ivee_executable_format {
    /**
     * Flat binary image without header and entry point at offset 0
     */
    IVEE_EXEC_BIN = 0,

    /**
     * Let the implementation guess the format
     */
    IVEE_EXEC_ANY
} ivee_executable_format_t;

/**
 * Architectural state of a virtual cpu when switching to IVEE context.
 * Actual architecture to use for IVEE VCPU is always the same as host.
 */
typedef struct ivee_arch_state {
    uint64_t rax;
    uint64_t rbx;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t rbp;
    uint64_t r8;
    uint64_t r9;
    uint64_t r10;
    uint64_t r11;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
} ivee_arch_state_t;

/**
 * Opaque handle to an execution environment
 */
typedef struct ivee ivee_t;

/**
 * List supported platform capabilities
 */
uint64_t ivee_list_platform_capabilities(void);

/**
 * Create new execution environment container
 *
 * \caps        Enabled environment capabilities.
 * \ivee        On success initialized pointer to an execption environment.
 */
int ivee_create(ivee_capabilities_t caps, ivee_t** ivee);

/**
 * Destroy an execution environment
 */
void ivee_destroy(ivee_t* ivee);

/**
 * Load a binary image into an execution environment.
 *
 * Supported image types are:
 * - COM (raw binary image with entry point at 0)
 *
 * Implementation will attempt to guess the file format or fall back to COM if unusuccessfull unless
 * format is specified explicitly.
 *
 * Loading of external imports is not supported, all images should be statically linked.
 * Image will be loaded at the default address specified in the image binary.
 *
 * Once the image is loaded the execution environment becomes sealed (optionally memory is encrypted).
 *
 * \ivee        Execution environment to load binary into
 * \file        Path to executable
 * \format      Executable format or IVEE_EXEC_ANY to guess
 */
int ivee_load_executable(ivee_t* ivee, const char* file, ivee_executable_format_t format);

/**
 * Execute a synchronous call into an execution environment with the specified architectural cpu state.
 *
 * \ivee        Exection environment to run
 * \state       Architectural cpu state on input. Updated after execution finished.
 */
int ivee_call(ivee_t* ivee, ivee_arch_state_t* state);

#ifdef __cplusplus
}
#endif
