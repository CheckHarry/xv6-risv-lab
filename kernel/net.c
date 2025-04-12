#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "net.h"

// xv6's ethernet and IP addresses
static uint8 local_mac[ETHADDR_LEN] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
static uint32 local_ip = MAKE_IP_ADDR(10, 0, 2, 15);

// qemu host's ethernet address.
static uint8 host_mac[ETHADDR_LEN] = { 0x52, 0x55, 0x0a, 0x00, 0x02, 0x02 };

static struct spinlock netlock;

#define CIRCULAR_BUFFER_SIZE 16
struct circular_buffer {
  struct {
    char buf[128];
    int len;  
  } lenb[CIRCULAR_BUFFER_SIZE];
  struct spinlock cb_lock;
  uint32 size;

  int start;
  int cur;
  int end;
};

int circular_buffer_init(struct circular_buffer *cb) {
  initlock(&cb->cb_lock, "buf_lock");
  cb->start = 0;
  cb->cur = 0;

  return 0;
}

int circular_buffer_full(struct circular_buffer *cb) {
  int full = ((cb->cur + 1) % CIRCULAR_BUFFER_SIZE) == cb->start;
  return full;
}

int circular_buffer_empty(struct circular_buffer *cb) {
  int empty = (cb->cur == cb->start);
  return empty;
}

int circular_buffer_push(struct circular_buffer *cb, const char* buf, int len) {
  if (len > 128) return -2;
  if (circular_buffer_full(cb)) return -1;
  memmove(cb->lenb[cb->cur].buf, buf, len);
  cb->lenb[cb->cur].len = len;
  cb->cur = (cb->cur + 1) % CIRCULAR_BUFFER_SIZE;
  return 0;
}

int circular_buffer_pop(struct circular_buffer *cb, char* buf, int maxlen) {
  if (cb->start == cb->cur) {
    return -1;
  }
  maxlen = (maxlen > cb->lenb[cb->start].len) ? cb->lenb[cb->start].len : maxlen;
  memmove(buf, cb->lenb[cb->start].buf, maxlen);
  cb->start = (cb->start + 1) % CIRCULAR_BUFFER_SIZE;
  return maxlen;
}

void circular_buffer_destroy(struct circular_buffer *cb) {
  for (int i = 0; i < CIRCULAR_BUFFER_SIZE; i ++) {
    kfree(cb->lenb[i].buf);
  }
}

struct mapper_entry {
  int port;
  struct circular_buffer *cb;
};

struct mapper_entry mapper[16];
char *recv_buf;

void mapper_entry_init() {
  for (int i = 0 ; i < 16; i ++) {
    mapper[i].port = -i;
    mapper[i].cb = (struct circular_buffer *)kalloc();
    if (!mapper[i].cb) panic("mapper_entry_init");
    circular_buffer_init(mapper[i].cb);
  }
}

struct mapper_entry* mapper_alloc() {
  for (int i = 0; i < 16; i ++) {
    if (mapper[i].port == -1) return &mapper[i];
  }
  return 0;
}

struct mapper_entry* mapper_find(int i) {
  for (int i = 0; i < 16; i ++) {
    if (mapper[i].port == i) return &mapper[i];
  }
  return 0;
}

void
netinit(void)
{
  initlock(&netlock, "netlock");
  memset(mapper,0,sizeof(mapper));
  mapper_entry_init();
}


//
// bind(int port)
// prepare to receive UDP packets address to the port,
// i.e. allocate any queues &c needed.

uint64
sys_bind(void)
{
  //
  // Your code here.
  //
  //uint64 addr;
  int n;

  argint(0, &n);

  if (mapper_find(n)) return -1;
  
  struct mapper_entry *me = mapper_alloc();
  if (!me) return -1;
  me->port = n;

  return -1;
}

//
// unbind(int port)
// release any resources previously created by bind(port);
// from now on UDP packets addressed to port should be dropped.
//
uint64
sys_unbind(void)
{
  //
  // Optional: Your code here.
  //

  return 0;
}

//
// recv(int dport, int *src, short *sport, char *buf, int maxlen)
// if there's a received UDP packet already queued that was
// addressed to dport, then return it.
// otherwise wait for such a packet.
//
// sets *src to the IP source address.
// sets *sport to the UDP source port.
// copies up to maxlen bytes of UDP payload to buf.
// returns the number of bytes copied,
// and -1 if there was an error.
//
// dport, *src, and *sport are host byte order.
// bind(dport) must previously have been called.
//
uint64
sys_recv(void)
{
  //
  // Your code here.
  //
  int dport;
  uint64 src;
  uint64 sport;
  uint64 buf;
  int maxlen;

  argint(0, &dport);
  argaddr(1, &src);
  argaddr(2, &sport);  
  argaddr(3, &buf); 
  argint(4, &maxlen); 

  struct mapper_entry *me = mapper_find(dport);
  if (!me) return -1;

  int recv_len = 0;
  //acquire(&recvlock);
  while (1) {
    if (killed(myproc())) {
      //release(&recvlock);
      return -1;
    }


    acquire(&me->cb->cb_lock);
    if (!circular_buffer_empty(me->cb)) {
      char *recv_buf = kalloc();
      if (!recv_buf) {
        release(&me->cb->cb_lock);
        //release(&recvlock);
        return -1;
      }

      recv_len = circular_buffer_pop(me->cb, recv_buf, 128);
      struct ip *ip = (struct ip*) recv_buf;
      //if (ip->ip_p != IPPROTO_UDP) panic("only udp");
      struct udp *udp = (struct udp*) (ip + 1);
      const char *payload = (const char*) (udp + 1);
      recv_len = ntohs(udp -> ulen) - sizeof(struct udp);
      recv_len = recv_len > maxlen ? maxlen : recv_len;
      uint16 s = ntohs(udp->sport);
      uint32 p = htonl(ip->ip_src);
      if (either_copyout(1, src, &p, sizeof(p)) == -1) panic("sys_recv");
      if (either_copyout(1, sport, &s, sizeof(s)) == -1) panic("sys_recv");
      if (either_copyout(1,buf,(void*)payload,recv_len) == -1) panic("sys_recv");
      kfree(recv_buf);
      release(&me->cb->cb_lock);
      return recv_len;
    }
    sleep(me->cb, &me->cb->cb_lock);
    release(&me->cb->cb_lock);
  }
  //release(&recvlock);
  return -1;
}

// This code is lifted from FreeBSD's ping.c, and is copyright by the Regents
// of the University of California.
static unsigned short
in_cksum(const unsigned char *addr, int len)
{
  int nleft = len;
  const unsigned short *w = (const unsigned short *)addr;
  unsigned int sum = 0;
  unsigned short answer = 0;

  /*
   * Our algorithm is simple, using a 32 bit accumulator (sum), we add
   * sequential 16 bit words to it, and at the end, fold back all the
   * carry bits from the top 16 bits into the lower 16 bits.
   */
  while (nleft > 1)  {
    sum += *w++;
    nleft -= 2;
  }

  /* mop up an odd byte, if necessary */
  if (nleft == 1) {
    *(unsigned char *)(&answer) = *(const unsigned char *)w;
    sum += answer;
  }

  /* add back carry outs from top 16 bits to low 16 bits */
  sum = (sum & 0xffff) + (sum >> 16);
  sum += (sum >> 16);
  /* guaranteed now that the lower 16 bits of sum are correct */

  answer = ~sum; /* truncate to 16 bits */
  return answer;
}

//
// send(int sport, int dst, int dport, char *buf, int len)
//
uint64
sys_send(void)
{
  struct proc *p = myproc();
  int sport;
  int dst;
  int dport;
  uint64 bufaddr;
  int len;

  argint(0, &sport);
  argint(1, &dst);
  argint(2, &dport);
  argaddr(3, &bufaddr);
  argint(4, &len);

  int total = len + sizeof(struct eth) + sizeof(struct ip) + sizeof(struct udp);
  if(total > PGSIZE)
    return -1;

  char *buf = kalloc();
  if(buf == 0){
    printf("sys_send: kalloc failed\n");
    return -1;
  }
  memset(buf, 0, PGSIZE);

  struct eth *eth = (struct eth *) buf;
  memmove(eth->dhost, host_mac, ETHADDR_LEN);
  memmove(eth->shost, local_mac, ETHADDR_LEN);
  eth->type = htons(ETHTYPE_IP);

  struct ip *ip = (struct ip *)(eth + 1);
  ip->ip_vhl = 0x45; // version 4, header length 4*5
  ip->ip_tos = 0;
  ip->ip_len = htons(sizeof(struct ip) + sizeof(struct udp) + len);
  ip->ip_id = 0;
  ip->ip_off = 0;
  ip->ip_ttl = 100;
  ip->ip_p = IPPROTO_UDP;
  ip->ip_src = htonl(local_ip);
  ip->ip_dst = htonl(dst);
  ip->ip_sum = in_cksum((unsigned char *)ip, sizeof(*ip));

  struct udp *udp = (struct udp *)(ip + 1);
  udp->sport = htons(sport);
  udp->dport = htons(dport);
  udp->ulen = htons(len + sizeof(struct udp));

  char *payload = (char *)(udp + 1);
  if(copyin(p->pagetable, payload, bufaddr, len) < 0){
    kfree(buf);
    printf("send: copyin failed\n");
    return -1;
  }

  if (e1000_transmit(buf, total) == -1) {
    kfree(buf);
    printf("send: e1000_transmit\n");
    return -1;
  }

  return 0;
}

void
ip_rx(char *buf, int len)
{
  // don't delete this printf; make grade depends on it.
  static int seen_ip = 0;
  if(seen_ip == 0)
    printf("ip_rx: received an IP packet\n");
  seen_ip = 1;

  //
  // Your code here.
  //

  struct eth *eth = (struct eth *) buf;
  struct ip *ip = (struct ip *) (eth + 1);

  if (ip->ip_p == IPPROTO_UDP) {
    struct udp *udp = (struct udp *) (ip + 1);
    short dport = ntohs(udp->dport);
    struct mapper_entry *me = mapper_find(dport);
    if (me) {
      uint16 len = ntohs(ip->ip_len);
      acquire(&me->cb->cb_lock);
      int res = circular_buffer_push(me->cb, (const char*)ip, len);
      release(&me->cb->cb_lock);
      if (res == -2) {
        printf("TOO BIG\n");
      }
      wakeup(me->cb);
    }
  }
  kfree(buf);
}

//
// send an ARP reply packet to tell qemu to map
// xv6's ip address to its ethernet address.
// this is the bare minimum needed to persuade
// qemu to send IP packets to xv6; the real ARP
// protocol is more complex.
//
void
arp_rx(char *inbuf)
{
  static int seen_arp = 0;

  if(seen_arp){
    kfree(inbuf);
    return;
  }
  printf("arp_rx: received an ARP packet\n");
  seen_arp = 1;

  struct eth *ineth = (struct eth *) inbuf;
  struct arp *inarp = (struct arp *) (ineth + 1);

  char *buf = kalloc();
  if(buf == 0)
    panic("send_arp_reply");
  
  struct eth *eth = (struct eth *) buf;
  memmove(eth->dhost, ineth->shost, ETHADDR_LEN); // ethernet destination = query source
  memmove(eth->shost, local_mac, ETHADDR_LEN); // ethernet source = xv6's ethernet address
  eth->type = htons(ETHTYPE_ARP);

  struct arp *arp = (struct arp *)(eth + 1);
  arp->hrd = htons(ARP_HRD_ETHER);
  arp->pro = htons(ETHTYPE_IP);
  arp->hln = ETHADDR_LEN;
  arp->pln = sizeof(uint32);
  arp->op = htons(ARP_OP_REPLY);

  memmove(arp->sha, local_mac, ETHADDR_LEN);
  arp->sip = htonl(local_ip);
  memmove(arp->tha, ineth->shost, ETHADDR_LEN);
  arp->tip = inarp->sip;

  if (e1000_transmit(buf, sizeof(*eth) + sizeof(*arp)) == -1) {
    panic("arp_rx: e1000_transmit\n");
  }

  kfree(inbuf);
}

void
net_rx(char *buf, int len)
{
  struct eth *eth = (struct eth *) buf;

  if(len >= sizeof(struct eth) + sizeof(struct arp) &&
     ntohs(eth->type) == ETHTYPE_ARP){
    arp_rx(buf);
  } else if(len >= sizeof(struct eth) + sizeof(struct ip) &&
     ntohs(eth->type) == ETHTYPE_IP){
    ip_rx(buf, len);
  } else {
    kfree(buf);
  }
}
