struct buf {
  int valid;   // has data been read from disk?
  int disk;    // does disk "own" buf?
  uint dev;
  uint blockno;
  struct spinlock splock;
  struct sleeplock lock;
  uint refcnt;
  uchar data[BSIZE];
  int index;
};

