/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 * 
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused. Realloc is
 * implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)


#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))
#define WSIZE 4 //Word
#define DSIZE 8
#define GET(p) (*(size_t *)(p)) // read value
#define PUT(p,val) (*(size_t *)(p) = (val)) // write value at pointer
#define SET(p,ptr) (*(size_t *)(p) = (long)ptr)
#define PACK(size,alloc) ((size)|(alloc)) // pack a size and allocated bit into a word
#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)
#define HDRP(bp) ((char *)(bp) - WSIZE) // header address
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - ALIGNMENT)
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - ALIGNMENT)))
#define PRED(bp) ((char *)(bp))
#define SUCC(bp) ((char *)(bp) + WSIZE)
#define PREDSG(bp) (*(char **)PRED(bp))
#define SUCCSG(bp) (*(char **)SUCC(bp))
#define CHUNKSIZE (1<<12)
#define LISTSIZE 16 // change list size.
#define MAX(x,y) ((x)>(y) ? (x):(y))

static void *coalesce(void *bp);
static void *extend_heap(size_t words);
static void *find_fit(size_t asize);
static void *place(void *bp, size_t asize);
static void insert(char *ptr, size_t size);
static void delete(char *ptr);
char *root = NULL;
void *sglist[LISTSIZE];
/* 
 * mm_init - initialize the malloc package.
 */

static void *coalesce(void *bp)
{   
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));
    
    if (prev_alloc && next_alloc){
        return bp;
    }
    else if (prev_alloc && !next_alloc){
        delete(NEXT_BLKP(bp));
        delete(bp);
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size,0));
        PUT(FTRP(bp), PACK(size,0));
    }
    else if (!prev_alloc && next_alloc){
        delete(PREV_BLKP(bp));
        delete(bp);
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(FTRP(bp), PACK(size,0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size,0));
        bp = PREV_BLKP(bp);
    }
    else {
        delete(PREV_BLKP(bp));
        delete(NEXT_BLKP(bp));
        delete(bp);
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size,0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size,0));
        bp = PREV_BLKP(bp);
    }
    insert(bp, size);
    return bp;
}

static void insert(char *ptr, size_t size)
{   
    int list = 0;
    char *last = NULL;
    char *pos;
    while ((list < LISTSIZE - 1) && (size > 1)){
        size >>=1;
        list++;
    }
    pos = sglist[list];
    while((pos != NULL) && (size>GET_SIZE(HDRP(pos)))){
        last = pos;
        pos = PREDSG(pos);
    }
    if ((pos == NULL) && (last == NULL)){
        SET(SUCC(ptr),NULL);
        SET(PRED(ptr),NULL);
        sglist[list] = ptr;
    }
    else if ((pos == NULL) && (last != NULL)){
        SET(PRED(ptr),NULL);
        SET(SUCC(ptr),last);
        SET(PRED(last),ptr);
    }
    else if ((pos != NULL) && (last == NULL)){
        SET(SUCC(pos),ptr);
        SET(PRED(ptr),pos);
        SET(SUCC(ptr),NULL);
        sglist[list] = ptr;
    }
    else{
        SET(SUCC(pos),ptr);
        SET(PRED(ptr),pos);
        SET(SUCC(ptr),last);
        SET(PRED(last),ptr);
    }
    return;
}

static void delete(char *ptr)
{
    int list = 0;
    size_t size = GET_SIZE(HDRP(ptr));

    while ((list < LISTSIZE - 1) && (size > 1)){
        size >>=1;
        list++;
    }
    if ((PREDSG(ptr) != NULL) && (SUCCSG(ptr) != NULL)){
        SET(PRED(SUCCSG(ptr)),PREDSG(ptr));
        SET(SUCC(PREDSG(ptr)),SUCCSG(ptr));
    }
    else if ((PREDSG(ptr) == NULL) && (SUCCSG(ptr) != NULL)){
        SET(PRED(SUCCSG(ptr)),NULL);
    }
    else if ((PREDSG(ptr) != NULL) && (SUCCSG(ptr) == NULL)){
        SET(SUCC(PREDSG(ptr)),NULL);
        sglist[list]=PREDSG(ptr);
    }
    else{
        sglist[list]=NULL;
    }
    return;
}


static void *extend_heap(size_t words)
{
    char *bp;
    size_t size;

    size =(words%2)?(words+1)*WSIZE:words*WSIZE;

    if ((long)(bp = mem_sbrk(size)) == -1)
        return NULL;

    PUT(HDRP(bp), PACK(size,0));
    PUT(FTRP(bp), PACK(size,0));
    PUT(bp + size - WSIZE,1);

    insert(bp, size);
    return coalesce(bp);
}

int mm_init(void)
{
    if ((root = mem_sbrk(4*WSIZE)) == (void *)-1){
        return -1;
    }
    for(int i = 0; i<LISTSIZE; i++){
        sglist[i] = NULL;
    }    


    PUT(root, 0); //root
    PUT(root + (1*WSIZE), PACK(DSIZE, 1));
    PUT(root + (2*WSIZE), PACK(DSIZE, 1));
    PUT(root + (3*WSIZE), PACK(0, 1));

    if (extend_heap(CHUNKSIZE/(2*DSIZE)) == NULL)
        return -1;    
    return 0;
}
/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
static void *find_fit(size_t callsize)
{  
    void *bp;
    int list = 0;
    size_t index = callsize;
    while((list<LISTSIZE)){
        if(list == LISTSIZE -1){
            bp = sglist[list];
            while((bp!=NULL)&&(callsize>GET_SIZE(HDRP(bp)))){
                bp = PREDSG(bp);
            }
            if(bp != NULL){
                return bp;
            }
        }
        if((sglist[list]!=NULL)&&(index<=1)){
            bp = sglist[list];
            while((bp!=NULL)&&(callsize>GET_SIZE(HDRP(bp)))){
                bp = PREDSG(bp);
            }
            if(bp != NULL){
                return bp;
            }
        }
        index >>=1;
        list += 1;
    }
    return bp;
}

static void *place(void *bp, size_t asize)
{
    size_t csize = GET_SIZE(HDRP(bp));
    delete(bp);

    if((csize - asize)>=(2*DSIZE)){
        if(asize<100){
            PUT(HDRP(bp),PACK(asize,1));
            PUT(FTRP(bp),PACK(asize,1));
            void *nbp = NEXT_BLKP(bp);
            PUT(HDRP(nbp),PACK(csize-asize,0));
            PUT(FTRP(nbp),PACK(csize-asize,0));
            insert(nbp,csize-asize);
            return bp;
        }
        else{
            PUT(HDRP(bp),PACK(csize-asize,0));
            PUT(FTRP(bp),PACK(csize-asize,0));
            void *nbp = NEXT_BLKP(bp);
            PUT(HDRP(nbp),PACK(asize,1));
            PUT(FTRP(nbp),PACK(asize,1));
            insert(bp,csize-asize);
            return nbp;
        }
    }
    else{
        PUT(HDRP(bp),PACK(csize,1));
        PUT(FTRP(bp),PACK(csize,1));
        return bp;
    }
}


void *mm_malloc(size_t size)
{      
    size_t callsize;
    size_t extendsize;
    char *bp;

    // Ignore spurious requests
    if (size<=0){
        return NULL;
    }
    // Adjust block size to include overhead and alignment reqs
    if (size <= DSIZE){
        callsize = 2*DSIZE;
    }
    else{
        callsize = ALIGN(size+DSIZE);
    }
    // search the free list for a fit
    if ((bp = find_fit(callsize)) != NULL){
        bp = place(bp,callsize);
        return bp;
    }
    //no right place
    extendsize = MAX(callsize,CHUNKSIZE);
    if ((bp = extend_heap(extendsize))==NULL){
        return NULL;
        }
    bp = place(bp, callsize);
    return bp;
}
/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *bp)
{
    size_t size = GET_SIZE(HDRP(bp));

    PUT(HDRP(bp),PACK(size,0));
    PUT(FTRP(bp),PACK(size,0));
    insert(bp,size);
    coalesce(bp);
    return;
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{
    void *oldptr = ptr;
    void *newptr;
    size_t oldsize = GET_SIZE(HDRP(ptr));
    size_t callsize = ALIGN(size + DSIZE);
    size_t capacity;
    size_t extendsize;
    
    if (oldsize == callsize){
        return ptr;
    }
    else if ((oldsize < callsize)&&(!GET_ALLOC(HDRP(NEXT_BLKP(ptr))))){
        capacity = oldsize + GET_SIZE(HDRP(NEXT_BLKP(ptr)));
        if(capacity>=callsize){
            delete(NEXT_BLKP(ptr));
            if ((capacity - callsize)>=2*DSIZE) {
                PUT(HDRP(ptr),PACK(callsize,1));
                PUT(FTRP(ptr),PACK(callsize,1));
                PUT(HDRP(NEXT_BLKP(ptr)),PACK(capacity-callsize,0));
                PUT(FTRP(NEXT_BLKP(ptr)),PACK(capacity-callsize,0));
                insert(NEXT_BLKP(ptr),capacity-callsize);
                return ptr;
            }
            else {
                PUT(HDRP(ptr),PACK(capacity,1));
                PUT(FTRP(ptr),PACK(capacity,1));
                return ptr;
            }
        }
        else{
            newptr = mm_malloc(size);
            if (newptr == NULL){
                extendsize = MAX(callsize,CHUNKSIZE);
                if((newptr = extend_heap(extendsize))==NULL){
                    return NULL;
                }
                newptr = place(newptr,callsize);
            }
            memcpy(newptr, oldptr, size);
            mm_free(oldptr);
            return newptr;
        }
    }
    else{
        newptr = mm_malloc(size);
        if (newptr == NULL){
            return NULL;
        }
        memcpy(newptr, oldptr, size);
        mm_free(oldptr);
        return newptr;
    }
}


