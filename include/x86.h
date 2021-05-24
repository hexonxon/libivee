#pragma once

#define X86_PAGE_SHIFT      12
#define X86_PAGE_SIZE       (1ul << X86_PAGE_SHIFT)
#define X86_PTES_PER_PAGE   (X86_PAGE_SIZE / sizeof(uint64_t))

#define X86_PTE_PRESENT     (1ul << 0)
#define X86_PTE_RW          (1ul << 1)
#define X86_PTE_NX          (1ul << 63)

/**
 * x86 segment descriptor
 * This definition is not exactly how actual descriptor is laid out.
 */
struct x86_segment
{
    uint64_t base;
    uint32_t limit;
    uint16_t selector;
    uint8_t dpl;

    enum {
        X86_SEG_TYPE_DATA       = 0b0010,   /* Read/Write */
        X86_SEG_TYPE_CODE       = 0b1010,   /* Execute/Read */
        X86_SEG_TYPE_TSS32      = 0b1011,   /* 32/64-bit TSS */
        X86_SEG_TYPE_LDT        = 0b0010,   /* LDT */
        X86_SEG_TYPE_ACC        = (1 << 0), /* Accessed flag */
    };
    uint8_t type;

    enum {
        /** 64-bit code segment. If set, DB must be cleared. */
        X86_SEG_L   = (1 << 0),

        /** Available for use by system software */
        X86_SEG_AVL = (1 << 1),

        /** Default operation size (0 = 16-bit segment, 1 = 32-bit segment) */
        X86_SEG_DB  = (1 << 2),

        /** Granularity (0 = limit is in 1 byte blocks, 1 = limit is in 4KiB blocks) */
        X86_SEG_G   = (1 << 3),

        /** Segment present. Must be 1 for all valid segments. */
        X86_SEG_P   = (1 << 4),

        /** Segment type (0 = system; 1 = code/data) */
        X86_SEG_S   = (1 << 5),
    };
    uint8_t flags;
};

/**
 * x86 descriptor table
 */
struct x86_dtbl
{
    uint32_t base;
    uint16_t limit;
};

/**
 * Virtualized x86 cpu state
 */
struct x86_cpu_state
{
    uint64_t rax, rbx, rcx, rdx, rsi, rdi, rsp, rbp;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t rflags;
    uint64_t rip;

    struct x86_segment cs;
    struct x86_segment ds;
    struct x86_segment es;
    struct x86_segment fs;
    struct x86_segment gs;
    struct x86_segment ss;
    struct x86_segment tr;
    struct x86_segment ldt;

    struct x86_dtbl gdt, idt;

    uint32_t cr0, cr2, cr3, cr4;
    uint32_t efer;
    uint32_t apic_base;
};
