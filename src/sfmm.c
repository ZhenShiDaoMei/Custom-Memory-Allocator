#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sfmm.h"
#include <errno.h>

#define MIN_BLOCK_SIZE 32  //bytes
#define ALIGN_SIZE 8
#define PAGE_MEMORY 4096 //4096 bytes

#define Wsize 8 //bytes for header
#define Dsize 8 //bytes for block size

size_t heapSpaceUsed = 0;
size_t heapMemoryMax = 0;


void *getQuickList();
void *getNextFreeBlock();
void *getLastBlock();
int noLSB();
void addToQuickList();
void removeFooter();
void removeHeader();
void addFooter();
void freeListAdd();
void removeFromFreeList();
void removePrevNext();
int power();

sf_block *startPointer;
sf_block *currentEndPoint;

void *sf_malloc(size_t size) {
    if(sf_mem_start() == sf_mem_end()){       //set up free list
        //prologue
        sf_block *startHeap = sf_mem_grow();
        startHeap->header = 32 | THIS_BLOCK_ALLOCATED;
        //epilogue
        sf_block *endOfHeap = (sf_block*)((char*)sf_mem_end()-8);
        endOfHeap->header = 8;
        currentEndPoint = endOfHeap;
        //memory use
        heapMemoryMax += PAGE_MEMORY;
        heapSpaceUsed += 40;
        //initialize free lists
        for (int i = 0; i < NUM_FREE_LISTS; i++){
            sf_free_list_heads[i].body.links.next = &sf_free_list_heads[i];
            sf_free_list_heads[i].body.links.prev = &sf_free_list_heads[i];
        }
        //initialize big free block
        sf_block *bigFreeBlock = (sf_block*)((char*)startHeap + 32);
        startPointer = bigFreeBlock;
        bigFreeBlock->header = 4056;
        //next/prev of big free block
        sf_free_list_heads[7].body.links.prev = bigFreeBlock;
        sf_free_list_heads[7].body.links.next = bigFreeBlock;
        bigFreeBlock->body.links.prev = &sf_free_list_heads[7];
        bigFreeBlock->body.links.next = &sf_free_list_heads[7];
        //footer of the big free block
        sf_footer *footer = (sf_footer*)((char*) bigFreeBlock + noLSB(bigFreeBlock) - 8);
        *(footer) = bigFreeBlock->header;
    }

    size_t allocationSize;

    if(size == 0){
        return NULL;
    }
    if((size + 8) <= 32){
        allocationSize = 32;
    }else{
        if((size+8)%8 != 0){
            int temp = size/8;
            allocationSize = temp*8 + 2*8;
        }else{
            allocationSize = size + 8;
        }
    }
    //==========================================================================================================================
    //check allocation size if fits in quicklist
    int quickListIndex = (allocationSize - 32)/8;

    if(quickListIndex < 20){
        if(sf_quick_lists[quickListIndex].length > 1){
            sf_block *tempBlockReturn = sf_quick_lists[quickListIndex].first;
            sf_block *tempBlockNext = tempBlockReturn->body.links.next;
            //remove from quicklist
            sf_quick_lists[quickListIndex].first = tempBlockNext;
            tempBlockReturn->header &= ~0x4;
            sf_quick_lists[quickListIndex].length = sf_quick_lists[quickListIndex].length - 1;
            //change next value
            tempBlockReturn->body.links.next = NULL;
            //return payload
            return (sf_block*)((char*)tempBlockReturn + 8);
        }

        if(sf_quick_lists[quickListIndex].length == 1){
            sf_block *tempBlockReturn = sf_quick_lists[quickListIndex].first;
            //remove from quicklist
            sf_quick_lists[quickListIndex].first = NULL;
            tempBlockReturn->header &= ~0x4;
            sf_quick_lists[quickListIndex].length = sf_quick_lists[quickListIndex].length - 1;
            //change next value
            tempBlockReturn->body.links.next = NULL;
            //return payload
            return (sf_block*)((char*)tempBlockReturn + 8);                       // questions if we gotta delete memory space for next
        }
    }
    //============================================================================================================================
    //find index within free list to begin searching
    size_t M = 32;
    int freeListIndex = -1;
    for(int i = 0 ; i <NUM_FREE_LISTS; i++){
        if(allocationSize < M){
            freeListIndex = i - 1;
            break;
        }
        M = M*2;
    }
    if(freeListIndex == -1){
        freeListIndex = 9;
    }
    sf_block *answerFreeBlock = NULL;
    //search free list begginning at index
    if(freeListIndex != -1){
        for(int i = freeListIndex; i < NUM_FREE_LISTS; i++){
            sf_block *loopBlock = sf_free_list_heads[i].body.links.next;
            while(loopBlock != &sf_free_list_heads[i]){
                if (allocationSize <= (loopBlock->header)){
                    answerFreeBlock = loopBlock;
                    break;
                }
                loopBlock = loopBlock->body.links.next;
            }
            if(answerFreeBlock != NULL){
                break;
            }
        }
    }
    //if there was a free block in the previous search answerFreeBlock == the free block, else nothing big enough in free list
    if(answerFreeBlock != NULL){
        if(((noLSB(answerFreeBlock)) - allocationSize) >= 32){            //leftover space
            size_t leftoverSpace = (noLSB(answerFreeBlock) - allocationSize);
            size_t M = 32;
            int freeListIndex = 0;
            //find which index the leftover remaining free block can go in free list
            for(int i = 0 ; i <NUM_FREE_LISTS; i++){
                if(leftoverSpace <= M){
                    freeListIndex = i;
                    break;
                }
                M = M*2;
            }
            //remove footer from old
            sf_footer *footerOriginal = (sf_footer*)((char*)answerFreeBlock + noLSB(answerFreeBlock) - 8);
            *(footerOriginal) = 0;
            //remove big free block
            sf_block *prevFreeBlock = answerFreeBlock->body.links.prev;
            sf_block *nextFreeBlock = answerFreeBlock->body.links.next;
            prevFreeBlock->body.links.next = nextFreeBlock;
            nextFreeBlock->body.links.prev = prevFreeBlock;
            //add leftover space to freelist
            sf_block *tempFreeBlock = (sf_block*)((char*)answerFreeBlock + allocationSize);
            tempFreeBlock->header = leftoverSpace| PREV_BLOCK_ALLOCATED;
            //set next/prev for leftover
            tempFreeBlock->body.links.next = &sf_free_list_heads[freeListIndex];
            sf_block *prevBlock = sf_free_list_heads[freeListIndex].body.links.prev;
            sf_free_list_heads[freeListIndex].body.links.prev = tempFreeBlock;
            prevBlock->body.links.next = tempFreeBlock;
            tempFreeBlock->body.links.prev = prevBlock;
            //add footer to new
            sf_footer *footer = (sf_footer*)((char*)tempFreeBlock + leftoverSpace - 8);
            *(footer) = tempFreeBlock->header;
            //return allocation memory block payload
            heapSpaceUsed += allocationSize;
            answerFreeBlock->header = allocationSize| THIS_BLOCK_ALLOCATED;
            answerFreeBlock->body.links.next = NULL;
            answerFreeBlock->body.links.prev = NULL;
            return (sf_block*)((char*)answerFreeBlock + 8);
        }else{
            //remove footer from old
            sf_footer *footerOriginal = (sf_footer*)((char*)answerFreeBlock + noLSB(answerFreeBlock) - 8);
            *(footerOriginal) = 0;
            //remove big free block
            sf_block *prevFreeBlock = answerFreeBlock->body.links.prev;
            sf_block *nextFreeBlock = answerFreeBlock->body.links.next;
            prevFreeBlock->body.links.next = nextFreeBlock;
            nextFreeBlock->body.links.prev = prevFreeBlock;
            //return allocation memory block payload
            answerFreeBlock->header = answerFreeBlock->header| THIS_BLOCK_ALLOCATED;
            answerFreeBlock->body.links.next = NULL;
            answerFreeBlock->body.links.prev = NULL;
            heapSpaceUsed += noLSB(answerFreeBlock);
            return (sf_block*)((char*)answerFreeBlock + 8);
        }
    }
    //============================================================================================================================
    //not enough space in freelist or quicklist, need to add more to heap.
    sf_block *pointer = getLastBlock(startPointer);
    if(pointer->header & THIS_BLOCK_ALLOCATED){         //last block in heap is allocated
        currentEndPoint->header = 0;
        sf_block *pointer = sf_mem_grow();
        if(pointer == NULL){
            sf_errno = ENOMEM;
            return NULL;
        }
        pointer->header = PAGE_MEMORY;
        heapMemoryMax += PAGE_MEMORY;
    }else{                                              //last block in list is not allocated, but does not have enough space.
        currentEndPoint->header = 0;
        sf_block *temp = sf_mem_grow();
        if(temp == NULL){
            sf_errno = ENOMEM;
            return NULL;
        }
        //remove footer
        sf_footer *footerOriginal = (sf_footer*)((char*) pointer + noLSB(pointer) - 8);
        *(footerOriginal) = 0;


        pointer->header = noLSB(pointer) + PAGE_MEMORY;
        heapMemoryMax += PAGE_MEMORY;
    }
    while(noLSB(pointer)<allocationSize){
        sf_block *temp = sf_mem_grow();
        if(temp == NULL){
            sf_errno = ENOMEM;
            return NULL;
        }
        sf_footer *footerRemove = (sf_footer*)((char*) pointer + noLSB(pointer) - 8);
        *(footerRemove) = 0;
        pointer->header = noLSB(pointer) + PAGE_MEMORY;
        heapMemoryMax += PAGE_MEMORY;
        sf_footer *footerAdd = (sf_footer*)((char*) pointer + noLSB(pointer) - 8);
        *(footerAdd) = pointer->header;
    }
    sf_block *end = sf_mem_end()-8;                       //fixes mem end 0 heap size
    end->header = 8;
    currentEndPoint = end;

    if(((noLSB(pointer)) - allocationSize) >= 32){            //free largeblock can be split after allocating
        size_t leftoverSpaceMem = (noLSB(pointer) - allocationSize);
        size_t M = 32;
        int ListIndex = -1;
        //find which index the leftover remaining free block can go in free list
        for(int i = 0 ; i <NUM_FREE_LISTS; i++){
            if(leftoverSpaceMem < M){
                break;
            }
            ListIndex = i;
            M = M*2;
        }
        if (ListIndex == -1){
            ListIndex = 9;
        }
        if (pointer->body.links.next != NULL){
        //remove next/prev
        sf_block *prevFreeBlock = pointer->body.links.prev;
        sf_block *nextFreeBlock = pointer->body.links.next;
        prevFreeBlock->body.links.next = nextFreeBlock;
        nextFreeBlock->body.links.prev = prevFreeBlock;
        }
        //add leftover space to freelist
        sf_block *freeBlock = (sf_block*)((char*)pointer + allocationSize);
        freeBlock->header = leftoverSpaceMem| PREV_BLOCK_ALLOCATED;
        //set next/prev for leftover
        freeBlock->body.links.next = &sf_free_list_heads[ListIndex];
        sf_block *prev = sf_free_list_heads[ListIndex].body.links.prev;
        sf_free_list_heads[ListIndex].body.links.prev = freeBlock;
        prev->body.links.next = freeBlock;
        freeBlock->body.links.prev = prev;
        //add footer to new
        sf_footer *footer = (sf_footer*)((char*)freeBlock + leftoverSpaceMem - 8);
        *(footer) = freeBlock->header;
        //return allocation memory block payload
        heapSpaceUsed += allocationSize;
        pointer->header = allocationSize| THIS_BLOCK_ALLOCATED;
        pointer->body.links.next = NULL;
        pointer->body.links.prev = NULL;
        return (sf_block*)((char*)pointer + 8);
    }else{
        if (pointer->body.links.next != NULL){
        //remove next/prev
        sf_block *prevFreeBlock = pointer->body.links.prev;
        sf_block *nextFreeBlock = pointer->body.links.next;
        prevFreeBlock->body.links.next = nextFreeBlock;
        nextFreeBlock->body.links.prev = prevFreeBlock;
        }
        //return allocation memory block payload
        pointer->header = pointer->header| THIS_BLOCK_ALLOCATED;
        pointer->body.links.next = NULL;
        pointer->body.links.prev = NULL;
        heapSpaceUsed += noLSB(pointer);
        return (sf_block*)((char*)pointer + 8);
    }
}

void sf_free(void *pp) {
    sf_block *pointer = ((sf_block*)pp);
    pointer = (sf_block*)((char*)pointer - 8);
    if(!(pointer->header & THIS_BLOCK_ALLOCATED) || pointer->header & IN_QUICK_LIST){
        abort();;
    }
    if(startPointer != NULL){
        if(pointer < startPointer){
            abort();
        }
    }

    if(pointer->header < 32){
        abort();
    }
    size_t blockSize = noLSB(pointer);
    int quickListIndex = (blockSize - 32)/8;

    if(quickListIndex < 20){
        addToQuickList(pointer,quickListIndex);
        return;
    }
    //pointer is middle block
    sf_block *maybePrevBlock = (sf_block*)((char*)pointer - 8);
    sf_block *maybeNextBlock = (sf_block*)((char*)pointer + noLSB(pointer));
    //both blocks are free
    if((maybePrevBlock->header >= 32 && (maybePrevBlock->header%8 == 0)) && (maybeNextBlock->header >= 32 && (maybeNextBlock->header%8 == 0))){
        sf_block *headerPrev = (sf_block*)((char*)maybePrevBlock - noLSB(maybePrevBlock) +8); //maybe change this
        //remove footer of prev and next
        size_t middle = noLSB(pointer);
        size_t after = noLSB(maybeNextBlock);
        removeHeader(pointer);

        removeFooter(maybeNextBlock);
        removeFromFreeList(maybeNextBlock);
        removePrevNext(maybeNextBlock);
        removeHeader(maybeNextBlock);

        removeFooter(headerPrev);
        removeFromFreeList(headerPrev);
        removePrevNext(headerPrev);

        headerPrev->header = headerPrev->header + middle + after;
        addFooter(headerPrev);
        freeListAdd(headerPrev);
        return;
    }
    //only prev block is free
    if((maybePrevBlock->header >= 32 && (maybePrevBlock->header%8 == 0))){
        sf_block *headerPrev = (sf_block*)((char*)maybePrevBlock - noLSB(maybePrevBlock) +8);
        size_t middle = noLSB(pointer);
        removeHeader(pointer);

        removeFooter(headerPrev);
        removeFromFreeList(headerPrev);
        removePrevNext(headerPrev);

        headerPrev->header = headerPrev->header + middle;
        addFooter(headerPrev);
        freeListAdd(headerPrev);
        return;
    }
    //only next block is free
    if((maybeNextBlock->header >= 32 && (maybeNextBlock->header%8 == 0))){
        size_t after = noLSB(maybeNextBlock);

        removeFooter(maybeNextBlock);
        removeFromFreeList(maybeNextBlock);
        removePrevNext(maybeNextBlock);
        removeHeader(maybeNextBlock);

        pointer->header = pointer->header + after;
        pointer->header &= ~0x1;
        addFooter(pointer);
        freeListAdd(pointer);
        return;
    }
    pointer->header &= ~0x1;
    addFooter(pointer);
    freeListAdd(pointer);
    return;
    abort();        //havent check 8-byte alligned
}

void *sf_realloc(void *pp, size_t rsize) {
    sf_block *pointer = ((sf_block*)pp);
    pointer = (sf_block*)((char*)pointer - 8);
    if(!(pointer->header & THIS_BLOCK_ALLOCATED) || pointer->header & IN_QUICK_LIST){
        abort();;
    }
    if(startPointer != NULL){
        if(pointer < startPointer){
            abort();
        }
    }

    if(pointer->header < 32){
        abort();
    }
    if(rsize == 0){
        sf_free(pp);
        return NULL;
    }
    abort();
}

void *sf_memalign(size_t size, size_t align) {
    if(size == 0){
        return NULL;
    }
    if(power(align) == 0 || align < 8){
        sf_errno = EINVAL;
        return NULL;
    }
    abort();
}

void *getQuickList(int quickListMemsize, int quickListIndex){
    if(sf_quick_lists[quickListMemsize].length != 0){
        sf_block *tempBlock = sf_quick_lists[quickListMemsize].first; //temp = first
        for(int i = 0 ; i < quickListIndex-1; i++){
            tempBlock = tempBlock->body.links.next;                 //temp = first.next
        }
        return tempBlock;
    }
    return NULL;
}

void *getNextFreeBlock(sf_block *startHeap){
    sf_block *current = startHeap;
    int counter = 0;
    while(current->header %2 == 1){
        int steps = ((current->header)/8) *8;
        current = current + steps;
        counter += steps;
    }
    return current;
}

void *getLastBlock(sf_block *startHeap){
    sf_block *current = startHeap;
    sf_block *prevCurrent;
    while(current->header != 8){
        sf_block *temp = (sf_block*)((char*)current + noLSB(current));
        if(temp == currentEndPoint){
            return current;
        }
        prevCurrent = current;
        current = current + noLSB(current);
    }
    return prevCurrent;
}

int noLSB(sf_block *pointer){
    int temp = pointer->header;
    temp = temp>>3;
    temp = temp<<3;
    return temp;
}

void addToQuickList(sf_block *pointer, int quickListIndex){
    pointer->header = pointer->header | IN_QUICK_LIST;
    if(sf_quick_lists[quickListIndex].length >= 5){ // WORK ON THE FLUSH
        while(sf_quick_lists[quickListIndex].length != 0){
            sf_block *first = sf_quick_lists[quickListIndex].first;
            sf_block *next = first->body.links.next;
            sf_quick_lists[quickListIndex].first = next;
            first->header &= ~0x1;
            first->header &= ~0x4;
            addFooter(first);
            freeListAdd(first);
            sf_quick_lists[quickListIndex].length = sf_quick_lists[quickListIndex].length - 1;
        }
        sf_block *temp = sf_quick_lists[quickListIndex].first;
        sf_quick_lists[quickListIndex].first = pointer;
        pointer->body.links.next = temp;
        return;
    }
    sf_block *temp = sf_quick_lists[quickListIndex].first;
    sf_quick_lists[quickListIndex].first = pointer;
    pointer->body.links.next = temp;
    return;
}

void removeFooter(sf_block *pointer){
    sf_footer *footerRemove = (sf_footer*)((char*) pointer + noLSB(pointer) - 8);
    *(footerRemove) = 0;
}

void removeHeader(sf_block *pointer){
    sf_header *headerRemove = (sf_header*)((char*) pointer);
    *(headerRemove) = 0;
}

void addFooter(sf_block *pointer){
    sf_footer *footerAdd = (sf_footer*)((char*)pointer + noLSB(pointer) - 8);
    *(footerAdd) = pointer->header;
}

void freeListAdd(sf_block *pointer){
    size_t space = pointer->header;
    size_t M = 32;
    int freeListIndex = -1;
    for(int i = 0 ; i <NUM_FREE_LISTS; i++){
        if(space <= M){
            freeListIndex = i;
            break;
        }
        M = M*2;
    }
    pointer->body.links.next = &sf_free_list_heads[freeListIndex];
    sf_block *prevBlock = sf_free_list_heads[freeListIndex].body.links.prev;
    sf_free_list_heads[freeListIndex].body.links.prev = pointer;
    prevBlock->body.links.next = pointer;
    pointer->body.links.prev = prevBlock;
}

void removeFromFreeList(sf_block *pointer){
    sf_block *prevFreeBlock = pointer->body.links.prev;
    sf_block *nextFreeBlock = pointer->body.links.next;
    prevFreeBlock->body.links.next = nextFreeBlock;
    nextFreeBlock->body.links.prev = prevFreeBlock;
}

void removePrevNext(sf_block *pointer){
    pointer->body.links.next = NULL;
    pointer->body.links.prev = NULL;
}

int power(int align){
    if (align == 0)
        return 0;
    while( align != 1){
        if(align % 2 != 0){
            return 0;
            align /= 2;
        }
    }
    return 1;
}
