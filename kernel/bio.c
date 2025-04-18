// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

struct {
  struct spinlock lock;
  struct buf buf[NBUF];
} bcache;

void
binit(void)
{
  initlock(&bcache.lock, "bcache");

  // Create linked list of buffers
  for(int i = 0; i < NBUF; i ++){
    bcache.buf[i].index = i;
    initlock(&bcache.buf[i].splock, "bcache.bucket");
    initsleeplock(&bcache.buf[i].lock, "buffer");
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{

  for (int i = 0; i < NBUF; i ++) {
    if (bcache.buf[i].dev != dev || bcache.buf[i].blockno != blockno) continue;
    acquire(&bcache.buf[i].splock);
    if (bcache.buf[i].dev == dev && bcache.buf[i].blockno == blockno) {
      bcache.buf[i].refcnt ++;
      release(&bcache.buf[i].splock);
      acquiresleep(&bcache.buf[i].lock);
      return &bcache.buf[i];
    }
    release(&bcache.buf[i].splock);
  }

  for (int i = 0; i < NBUF; i ++) {
    if (bcache.buf[i].refcnt) continue;
    acquire(&bcache.buf[i].splock);   
    if (bcache.buf[i].refcnt == 0) {
      bcache.buf[i].dev = dev;
      bcache.buf[i].blockno = blockno;
      bcache.buf[i].valid = 0;
      bcache.buf[i].refcnt = 1;
      release(&bcache.buf[i].splock);
      acquiresleep(&bcache.buf[i].lock);
      return &bcache.buf[i];
    }
    release(&bcache.buf[i].splock);
  }
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  acquire(&b->splock);
  b->refcnt--;
  release(&b->splock);
}

void
bpin(struct buf *b) {
  acquire(&b->splock);
  b->refcnt++;
  release(&b->splock);
}

void
bunpin(struct buf *b) {
  acquire(&b->splock);
  b->refcnt--;
  release(&b->splock);
}


