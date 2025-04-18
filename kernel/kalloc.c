// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

#define MAX_STEAL_NUM 64

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct per_cpu_kmem {
  struct spinlock lock;
  struct run *freelist;
};

struct {
  struct spinlock lock;
  struct per_cpu_kmem percpukmem[NCPU];
} kmem;

void
kinit()
{
  for (int i = 0; i < NCPU; i ++) {
    initlock(&kmem.percpukmem[i].lock, "kmem");
    kmem.percpukmem[i].freelist = 0;
  }
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

void kfree_percpu(void *pa, struct per_cpu_kmem* mem) {
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&mem->lock);
  r->next = mem->freelist;
  mem->freelist = r;
  release(&mem->lock);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  push_off();
  int id = cpuid();
  pop_off();
  kfree_percpu(pa, &kmem.percpukmem[id]);
}

void *kalloc_percpu(struct per_cpu_kmem* mem) {
  struct run *r;

  acquire(&mem->lock);
  r = mem->freelist;
  if(r)
    mem->freelist = r->next;
  release(&mem->lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  push_off();
  int id = cpuid();
  pop_off();
  struct per_cpu_kmem *mem = &kmem.percpukmem[id];
  
  void *ret = kalloc_percpu(mem);
  if (ret) {
    return ret;
  }

  for (int i = 0; i < NCPU; i ++) {
    if (i != id) {
      ret = kalloc_percpu(&kmem.percpukmem[i]);
      if (ret) break;
    }
  }
  return ret;
}
