// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

#define INDEX(pa) ((void*)pa - (void*)end) / PGSIZE
#define ALLOC_DEBUG_SIZE (8 * 256 * 128 * 8)
#define ALLOC_DEBUG (PHYSTOP - ALLOC_DEBUG_SIZE)
#define ALLOC_DEBUG2 (ALLOC_DEBUG - 32 * 4096)
#define ALLOC_DEBUG3 (ALLOC_DEBUG2 - 32 * 4096)

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
  uint64 *alloc_debug;
  int alloc_id;
  int *debug_id;
  int *debug_id2;
  int enable_alloc_debug;
} kmem;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  kmem.alloc_debug = (uint64*)(ALLOC_DEBUG);
  kmem.debug_id = (int*)(ALLOC_DEBUG2);
  kmem.debug_id2 = (int*)(ALLOC_DEBUG3);
  kmem.alloc_id = 0;
  memset((void*)kmem.alloc_debug, 0, ALLOC_DEBUG_SIZE);
  kmem.enable_alloc_debug = 0;
  freerange(end, (void*)(ALLOC_DEBUG3));
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= (ALLOC_DEBUG3))
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  kmem.alloc_debug[INDEX(r) * 8] = 0;
  kmem.debug_id[INDEX(r)] = 0;
  release(&kmem.lock);
}

void kalloc_debug() {
  kmem.enable_alloc_debug = 1;
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
  {
    kmem.freelist = r->next;
    kmem.debug_id[INDEX(r)] = ++(kmem.alloc_id);
  }
  release(&kmem.lock);

  if(r)
  {
    memset((char*)r, 5, PGSIZE); // fill with junk
    backtrace_write(&kmem.alloc_debug[INDEX(r) * 8], 8);
  }
    
  return (void*)r;
}

void kmem_san_start() {
  acquire(&kmem.lock);
  int total = ((void*)(ALLOC_DEBUG3) - (void*)end) / PGSIZE;
  for (int i = 0; i < total; i ++) {
    kmem.debug_id2[i] = kmem.debug_id[i];
    // if (kmem.debug_id[i]) {
    //   printf("%d : %d\n", i, kmem.debug_id[i]);
    // }
  }
  release(&kmem.lock);
}

void kmem_san_end() {
  acquire(&kmem.lock);
  int total = ((void*)(ALLOC_DEBUG3) - (void*)end) / PGSIZE;
  for (int i = 0; i < total; i ++) {
   if (kmem.debug_id[i] && (kmem.debug_id2[i] != kmem.debug_id[i])) {
    printf("%d : %d\n", i, kmem.debug_id[i]);
    for (int j = 0 ; j < 8 ; j ++) {
      if (!kmem.alloc_debug[i * 8 + j]) break;
      printf("%p\n", (void*)kmem.alloc_debug[i * 8 + j]);
    }
   }
    // if (kmem.debug_id[i]) {
    //   printf("%d : %d\n", i, kmem.debug_id[i]);
    // }
  }
  release(&kmem.lock);
}