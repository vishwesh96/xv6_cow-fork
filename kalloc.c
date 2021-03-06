// Physical memory allocator, intended to allocate
// memory for user processes, kernel stacks, page table pages,
// and pipe buffers. Allocates 4096-byte pages.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "spinlock.h"

#define NFRAMES PHYSTOP/PGSIZE          //number of frames

void freerange(void *vstart, void *vend);
extern char end[]; // first address after kernel loaded from ELF file

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  int use_lock;
  struct run *freelist;
  int numFreePages;                 //added an interger to hold count of number of free pages
} kmem;

  
struct{                             //reference count structure
  int rcount[NFRAMES];
  struct spinlock lock;
} rtable;


void
rinit(void)                       //initialize reference count structure
{
  initlock(&rtable.lock, "rtable");
  acquire(&rtable.lock);
  int i;
  for(i = 0; i< NFRAMES ; i++){                               //initialize reference count to 0
    rtable.rcount[i] = 0; 
  }
  // cprintf("\ncpu%d: rinit called\n\n", cpunum());
  release(&rtable.lock);
}
  
void incrementRcount(uint pa){                    //increment reference count
  acquire(&rtable.lock);
  rtable.rcount[pa>>PGSHIFT]++;
  release(&rtable.lock);
}
void decrementRcount(uint pa){                  //decrement reference count
  acquire(&rtable.lock);
  rtable.rcount[pa>>PGSHIFT]--;
  release(&rtable.lock);
}


int getRcount(uint pa){                         //get reference count
  acquire(&rtable.lock);
  uint temp = rtable.rcount[pa>>PGSHIFT];
  release(&rtable.lock);
  return temp;
}

/*Lock must be held*/
void setRcount(uint pa,int i){                //set reference count
  rtable.rcount[pa>>PGSHIFT] = i;
}

int getNumFreePages(){                        //get  number of free pages
  if(kmem.use_lock){  
    acquire(&kmem.lock);
  }
  int temp = kmem.numFreePages;

  if(kmem.use_lock){
    release(&kmem.lock);
  }
  return temp;
}
// Initialization happens in two phases.
// 1. main() calls kinit1() while still using entrypgdir to place just
// the pages mapped by entrypgdir on free list.
// 2. main() calls kinit2() with the rest of the physical pages
// after installing a full page table that maps them on all cores.
void
kinit1(void *vstart, void *vend)
{
  cprintf("\ncpu%d: kinit1 called\n\n", cpunum());
  initlock(&kmem.lock, "kmem");
  kmem.use_lock = 0;
  kmem.numFreePages = 0;                        //initalize number of free pages
  freerange(vstart, vend);
}

void
kinit2(void *vstart, void *vend)
{
  freerange(vstart, vend);
  kmem.use_lock = 1;
}

void
freerange(void *vstart, void *vend)
{
  char *p;
  p = (char*)PGROUNDUP((uint)vstart);
  for(; p + PGSIZE <= (char*)vend; p += PGSIZE)
    kfree(p);
}

//PAGEBREAK: 21
// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(char *v)
{
  struct run *r;

  if((uint)v % PGSIZE || v < end || V2P(v) >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(v, 1, PGSIZE);

  if(kmem.use_lock){
    acquire(&kmem.lock);
    acquire(&rtable.lock);
  }
  r = (struct run*)v;
  r->next = kmem.freelist;
  kmem.freelist = r;
  kmem.numFreePages++;                            //increment free page count
  setRcount((uint)V2P((uint)r),0);                //set reference count to 0
  if(kmem.use_lock){
    release(&kmem.lock);
    release(&rtable.lock);
  }
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
char*
kalloc(void)
{
  struct run *r;

  if(kmem.use_lock){
    acquire(&kmem.lock);
    acquire(&rtable.lock);
  }
  r = kmem.freelist;
  if(r){
    kmem.freelist = r->next;
    kmem.numFreePages--;                         //increment free page count                           
    setRcount((uint)V2P((uint)r),1);             //set reference count to 1
  }

  if(kmem.use_lock){
    release(&kmem.lock);
    release(&rtable.lock);
  }

  return (char*)r;
}

