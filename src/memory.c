#include <stdlib.h>
#include <stdatomic.h>
#include <errno.h>
#include <sys/mman.h>

#include "platform.h"
#include "memory.h"
#include "kvm.h"

struct ivee_guest_memory_region* ivee_map_host_memory(struct ivee_memory_map* map,
                                                      gpa_t gpa,
                                                      size_t length,
                                                      int mmap_fd,
                                                      bool host_ro,
                                                      enum ivee_memory_prot prot)
{
    if (!map) {
        return NULL;
    }

    if (!length) {
        return NULL;
    }

    /* Check that region does not overflow the GPA space */
    if (IVEE_GPA_LAST - gpa < length - 1) {
        return NULL;
    }

    /* Fixup length to be page-aligned */
    length = (length + (PAGE_SIZE - 1)) & ~(PAGE_SIZE - 1);

    gpa_t first_gfn = gpa >> PAGE_SHIFT;
    gpa_t last_gfn = (gpa + (length - 1)) >> PAGE_SHIFT;

    /* Walk current regions and check for overlaps */
    struct ivee_guest_memory_region* mr;
    LIST_FOREACH(mr, &map->regions, link) {
        if (first_gfn <= mr->last_gfn && last_gfn >= mr->first_gfn) {
            return NULL;
        }
    }

    void* ptr = mmap(NULL,
                     length,
                     (host_ro ? PROT_READ : PROT_READ | PROT_WRITE),
                     MAP_SHARED | (mmap_fd == -1 ? MAP_ANONYMOUS : 0),
                     mmap_fd,
                     0);
    if (ptr == MAP_FAILED) {
        return NULL;
    }

    mr = ivee_alloc(sizeof(*mr));
    if (!mr) {
        munmap(ptr, length);
        return NULL;
    }

    mr->first_gfn = first_gfn;
    mr->last_gfn = last_gfn;
    mr->prot = prot;
    mr->hva = ptr;
    mr->length = length;

    LIST_INSERT_HEAD(&map->regions, mr, link);
    return mr;
}

void ivee_unmap_host_memory(struct ivee_guest_memory_region* mr)
{
    if (!mr || !mr->hva) {
        return;
    }

    LIST_REMOVE(mr, link);

    munmap(mr->hva, mr->length);
    ivee_free(mr);
}

int ivee_init_memory_map(struct ivee_memory_map* map)
{
    LIST_INIT(&map->regions);
    return 0;
}

void ivee_free_memory_map(struct ivee_memory_map* map)
{
    if (!map) {
        return;
    }

    struct ivee_guest_memory_region* mr;
    while (!LIST_EMPTY(&map->regions)) {
        mr = LIST_FIRST(&map->regions);
        ivee_unmap_host_memory(mr);
    }
}
