// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

#define BIGPAGE_MEM_RESERVE 32 * 1024 * 1024

void freerange(void *pa_start, void *pa_end);
void freerange_big(void *pa_start, void *pa_end);


extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem_big;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)(PHYSTOP - BIGPAGE_MEM_RESERVE));
  freerange_big((void*)(PHYSTOP - BIGPAGE_MEM_RESERVE), (void*)(PHYSTOP));
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}


void
freerange_big(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + SUPERPGSIZE <= (char*)pa_end; p += SUPERPGSIZE)
    kfree_big(p);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= (PHYSTOP  - BIGPAGE_MEM_RESERVE))
  {
    printf("kfree : %p\n" , pa);
    panic("kfree");
  }
    

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}


void
kfree_big(void *pa)
{
  struct run *r;

  if(((uint64)pa % SUPERPGSIZE) != 0 || (char*)pa < (char*)(PHYSTOP - BIGPAGE_MEM_RESERVE) || (uint64)pa >= PHYSTOP)
    panic("kfree big");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, SUPERPGSIZE);

  r = (struct run*)pa;

  acquire(&kmem_big.lock);
  r->next = kmem_big.freelist;
  kmem_big.freelist = r;
  release(&kmem_big.lock);
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
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}


void *
kalloc_big(void)
{
  struct run *r;

  acquire(&kmem_big.lock);
  r = kmem_big.freelist;
  if(r)
  kmem_big.freelist = r->next;
  release(&kmem_big.lock);

  if(r)
    memset((char*)r, 5, SUPERPGSIZE); // fill with junk
  return (void*)r;
}
