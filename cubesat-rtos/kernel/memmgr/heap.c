/*
 * heap.c
 *
 * Created: 06.05.2020 17:21:08
 *  Author: Admin
 *  Based on FreeRTOS heap_4 code
 */ 

#include <kernel/kernel.h>
#include <kernel/types.h>

#define CFG_HEAP_SIZE 2000 //TODO: move to config
#define CFG_PLATFORM_BYTE_ALIGNMENT 1
#define CFG_PLATFORM_BYTE_ALIGNMENT_MASK 0x0000
#define CFG_MIN_BLOCK_SIZE 4

static uint8_t kHeapRegion[CFG_HEAP_SIZE];

static const size_t kHeapStructSize	= (sizeof(struct kMemoryBlock_t) + ((size_t)(CFG_PLATFORM_BYTE_ALIGNMENT - 1))) & ~((size_t)CFG_PLATFORM_BYTE_ALIGNMENT_MASK); //What the hell is this I shouldn't have copied FreeRTOS code

static struct kMemoryBlock_t kHeapStart;
static struct kMemoryBlock_t* kHeapEnd;

static size_t kFreeMemory = 0;
static size_t kMinimumFreeMemory = 0;

size_t memmgr_getFreeHeap()
{
	return kFreeMemory;
}

size_t memmgr_getFreeHeapMin()
{
	return kMinimumFreeMemory;
}

void memmgr_heapInit()
{
	struct kMemoryBlock_t* firstFreeBlock;
	uint8_t* heapAligned;
	size_t heapAddress;
	size_t heapSize = CFG_HEAP_SIZE;
	
	heapAddress = (size_t)kHeapRegion;
	
	if ((heapAddress & CFG_PLATFORM_BYTE_ALIGNMENT_MASK) != 0) {
		heapAddress += (CFG_PLATFORM_BYTE_ALIGNMENT - 1);
		heapAddress &= ~((size_t)CFG_PLATFORM_BYTE_ALIGNMENT_MASK);
		heapSize -= heapAddress - (size_t)kHeapRegion;
	}
	
	heapAligned = (uint8_t*)heapAddress;
	
	kHeapStart.next = (void*)heapAligned;
	kHeapStart.blockSize = (size_t)0;
	kHeapStart.state = 0;

	heapAddress = ((size_t)heapAligned) + heapSize;
	heapAddress -= kHeapStructSize;
	heapAddress &= ~((size_t)CFG_PLATFORM_BYTE_ALIGNMENT_MASK);
	
	kHeapEnd = (void*)heapAddress;
	kHeapEnd -> blockSize = 0;
	kHeapEnd -> next = NULL;
	kHeapEnd -> state = 0;

	firstFreeBlock = (void*)heapAligned;
	firstFreeBlock -> blockSize = heapAddress - (size_t)firstFreeBlock;
	firstFreeBlock -> next = kHeapEnd;
	firstFreeBlock -> state = 0;

	kMinimumFreeMemory = firstFreeBlock -> blockSize;
	kFreeMemory = firstFreeBlock -> blockSize;
}

static void memmgr_insertFreeBlock(struct kMemoryBlock_t* blockToInsert)
{
	struct kMemoryBlock_t* blockIterator;
	uint8_t* pointer_casted;
	
	for (blockIterator = &kHeapStart; blockIterator -> next < blockToInsert; blockIterator = blockIterator -> next) {;} //What
	
	pointer_casted = (uint8_t*)blockIterator;
	
	if ((pointer_casted + blockIterator -> blockSize) == (uint8_t*)blockToInsert) {
		blockIterator -> blockSize += blockToInsert -> blockSize;
		blockToInsert = blockIterator;
	}
	
	pointer_casted = (uint8_t*)blockToInsert;
	
	if ((pointer_casted + blockToInsert -> blockSize) == (uint8_t*)blockIterator -> next) {
		if (blockIterator -> next != kHeapEnd) {
			blockToInsert -> blockSize += blockIterator -> next -> blockSize;
			blockToInsert -> next = blockIterator -> next -> next;
		}
		else {
			blockToInsert -> next = kHeapEnd;
		}
	}
	else {
		blockToInsert -> next = blockIterator -> next;
	}

	if(blockIterator != blockToInsert) {
		blockIterator -> next = blockToInsert;
	}
	
	return;
}

void* memmgr_heapAlloc(size_t size)
{
	void* returnAddress = NULL;
	struct kMemoryBlock_t *block, *newBlock, *previousBlock;
	
	kStatusRegister_t sreg = threads_startAtomicOperation();
	
	if (size > 0 && (size & CFG_PLATFORM_BYTE_ALIGNMENT_MASK) != 0x00) {
		size += (CFG_PLATFORM_BYTE_ALIGNMENT - (size & CFG_PLATFORM_BYTE_ALIGNMENT_MASK));
	}
	
	if (size > 0 && size <= kFreeMemory) {
		previousBlock = &kHeapStart;
		block = kHeapStart.next;
		
		while ((block -> blockSize < size) && (block -> next != NULL)) {
			previousBlock = block;
			block = block -> next;
		}
		
		if (block != kHeapEnd) {
			returnAddress = (void*)(((uint8_t*)previousBlock -> next) + kHeapStructSize); //Parenthesis hell
			
			previousBlock -> next = block -> next;

			if ((block -> blockSize - size) > CFG_MIN_BLOCK_SIZE) {
				newBlock = (void*)(((uint8_t*)block) + size);

				newBlock -> blockSize = block -> blockSize - size;
				block -> blockSize = size;

				memmgr_insertFreeBlock(newBlock);
			}

			kFreeMemory -= block -> blockSize;

			if(kFreeMemory < kMinimumFreeMemory) {
				kMinimumFreeMemory = kFreeMemory;
			}
			
			block -> state = 1;
			block -> next = NULL;
		}
	}
	
	threads_endAtomicOperation(sreg);
	return returnAddress;
}

void memmgr_heapFree(void* pointer)
{
	uint8_t* pointer_casted = (uint8_t*)pointer;
	struct kMemoryBlock_t* block;
	
	kStatusRegister_t sreg = threads_startAtomicOperation();
	
	if (pointer != NULL) {
		pointer_casted -= kHeapStructSize;
		
		block = (void*)pointer_casted;
		if (block -> state != 0) {
			if (block -> next == NULL) {
				block -> state = 0;
				kFreeMemory += block -> blockSize;
				memmgr_insertFreeBlock((struct kMemoryBlock_t*)block);
			}
		}
	}
	threads_endAtomicOperation(sreg);
	return;
}