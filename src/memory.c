#include <stdlib.h>
#include <stdatomic.h>
#include <errno.h>
#include <sys/mman.h>

#include "platform.h"
#include "memory.h"
#include "kvm.h"

int ivee_map_host_memory(size_t length, bool ro, int mmap_fd, struct ivee_host_memory_region* out_mr)
{
    if (length == 0 || !out_mr) {
        return -EINVAL;
    }

    void* ptr = mmap(NULL,
                     length,
                     (ro ? PROT_READ : PROT_READ | PROT_WRITE),
                     MAP_SHARED | (mmap_fd == -1 ? MAP_ANONYMOUS : 0),
                     mmap_fd,
                     0);
    if (ptr == MAP_FAILED) {
        return -errno;
    }

    out_mr->hva = ptr;
    out_mr->length = length;
    atomic_init(&out_mr->refcount, 1);

    return 0;
}

static void free_host_region(struct ivee_host_memory_region* r)
{
    munmap(r->hva, r->length);
    r->hva = NULL;
    r->length = 0;
}

static void take_host_region(struct ivee_host_memory_region* r)
{
    atomic_fetch_add(&r->refcount, 1);
}

static void drop_host_region(struct ivee_host_memory_region* r)
{
    if (atomic_fetch_sub(&r->refcount, 1) == 1) {
        free_host_region(r);
    }
}

void ivee_drop_host_memory(struct ivee_host_memory_region* r)
{
    if (r) {
        drop_host_region(r);
    }
}

int ivee_init_memory_map(struct ivee_memory_map* map)
{
    LIST_INIT(&map->regions);
    return 0;
}
 
int ivee_map_guest_region(struct ivee_memory_map* map, struct ivee_host_memory_region* host, gpa_t gpa, bool ro)
{
    if (!map) {
        return -EINVAL;
    }

    if (!host || !host->length) {
        return -EINVAL;
    }

    /* Check that region does not overflow the GPA space */
    if (IVEE_GPA_LAST - gpa < host->length - 1) {
        return -EINVAL;
    }

    gpa_t first_gfn = gpa >> 12;
    gpa_t last_gfn = (gpa + (host->length - 1)) >> 12;

    /* Walk current regions and check for overlaps */
    struct ivee_guest_memory_region* guest;
    LIST_FOREACH(guest, &map->regions, link) {
        if (first_gfn <= guest->last_gfn && last_gfn >= guest->first_gfn) {
            return -EINVAL;
        }
    }

    guest = ivee_alloc(sizeof(*guest));
    if (!guest) {
        return -ENOMEM;
    }

    guest->first_gfn = first_gfn;
    guest->last_gfn = last_gfn;
    guest->ro = ro;
    guest->host = host;

    LIST_INSERT_HEAD(&map->regions, guest, link);
    take_host_region(host);

    return 0;
}
