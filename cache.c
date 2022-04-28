#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "cache.h"

static cache_entry_t *cache = NULL;
static cache_entry_t *unfilledCurrentCacheIndex;
static int cache_size = 0;
static int numFilledCacheIndices = 0; 
static int clock = 0;
static int num_queries = 0;
static int num_hits = 0;
static bool cacheExists = false;
static bool cacheIsEmpty = true;
static bool cacheIsFull = false;

int cache_create(int num_entries) {

	bool parametersAreInvalid = !((num_entries >= 2) & (num_entries <= 4096)) | cacheExists;
	if (parametersAreInvalid)
	{
		return -1;
	} 
	
	else
	{
		// We allocate memory, amount based on the input cache num_entries. 
		cache_size = num_entries;
		cache = malloc(cache_size * sizeof(cache_entry_t));
		unfilledCurrentCacheIndex = cache;
		cacheExists = true;
		return 1;
  	}		
}


int cache_destroy(void) {
	if (!cacheExists)
	{
		return -1;
	}
	
	// Destoys cache, we do so by freeing the allocated memory for the cache and NULL'ing all pointers that point to addresses within this space.
	free(cache);
	cache = NULL;
	unfilledCurrentCacheIndex = NULL;
	numFilledCacheIndices = 0;
	cache_size = 0;
	cacheExists = false;
	return 1;
}


int cache_lookup(int disk_num, int block_num, uint8_t *buf) {
	/*
	 * Every look up increments query count, regardless of a cache hit or miss.
	 * Parameters of look up are invalid if the cache does not exist, is empty, or pointer to the buffer is NULL.
	 */
	bool blockFound;
	bool parametersAreValid = (cache != NULL) & (!cacheIsEmpty) & (buf != NULL);
	cache_entry_t *currentCacheIndex;
	currentCacheIndex = cache;
	num_queries++;
	
	if (parametersAreValid)
	{
		// Iterate through whole cache. Return 1 if cache entry found. Otherwise look up fails--a cache miss.
		for (int i = 0; i < cache_size; i++)
		{
			blockFound = (currentCacheIndex->disk_num == disk_num) & (currentCacheIndex->block_num == block_num);
			if (blockFound)
			{
				/* 
				 * Cache hit--we found the specified block in the cache. 
				 * We copy to the buffer and increment the number of cache hits
				 */
				num_hits++;
				memcpy(buf, currentCacheIndex->block, JBOD_BLOCK_SIZE);
				return 1;
			}
			
			currentCacheIndex++;
		}
		
		// Exited loop, so we have a cache miss--did not find the specified block in the cache. 
		return -1;
	}
	
	else{
		// Invalid parameters in cache look up.
 		return -1;
 	}
}


void cache_update(int disk_num, int block_num, const uint8_t *buf) {
	/* 
	 * In this implementation, we are utilizing the Last Recently Used (LRU) cache replacement policy
	 * A larger clock value corresponds to more recently referenced block.
	 * This function will be called when a cache hit occurs.
	 * Updated index is now the most recently used.
	 */
	bool blockFound;
	cache_entry_t *currentCacheIndex;
	currentCacheIndex = cache;
	
	for (int i = 0; i < cache_size; i++)
	{
		blockFound = (currentCacheIndex->disk_num == disk_num) & (currentCacheIndex->block_num == block_num);
		if (blockFound)
		{
			clock += 1;
			currentCacheIndex->access_time = clock;
			memcpy(currentCacheIndex->block, buf, JBOD_BLOCK_SIZE);
		}
		
		currentCacheIndex++;
	}
}


int cache_insert(int disk_num, int block_num, const uint8_t *buf) {
	/*
	 * The following are the parameter constraints for the insert function:
	 * Disk number must range from 0 to 15.
	 * Block number must range from 0 to 255.
	 * Pointer to cache CANNOT be NULL (cache must exists).
	 * Pointer to buffer CANNOT be NULL.
	 */
	bool parametersAreInvalid = !((disk_num >= 0) & (disk_num <= 15)) | !((block_num >= 0) & (block_num <= 255)) | (cache == NULL) | (buf == NULL);
	bool blockAlreadyInCache;
	cache_entry_t *currentCacheIndex;
	currentCacheIndex = cache;
	
	if (parametersAreInvalid)
	{
		return -1;
	}
	
	/*
	 * Iterate through the cache to see if the block already exists in the cache.
	 * If the block is already in the cache, we do not insert the same block into another cache index.
	 */
	for (int i = 0; i < cache_size; i++)
	{
		blockAlreadyInCache = (currentCacheIndex->disk_num == disk_num) & (currentCacheIndex->block_num == block_num) & (currentCacheIndex->valid == true);
		if (blockAlreadyInCache)
		{
			return -1;
		}
				
		currentCacheIndex++;
	}
	
	currentCacheIndex = cache;
	
	if (cacheIsFull)
	{	
		/*
	 	 * If the cache is full, we replace the least recently used index: the index with the smallest access time.
	 	 * We iterate through the cache to find the index with the smallest access time and store this location in indexOfSmallestAccessTime
		 */
		int minAccessTime = currentCacheIndex->access_time;
		cache_entry_t *indexOfSmallestAccessTime;
		indexOfSmallestAccessTime = currentCacheIndex;
		
		for (int i = 0; i < (cache_size - 1); i++)
		{ 				
			if (minAccessTime < currentCacheIndex->access_time)
			{
				minAccessTime = currentCacheIndex->access_time;
				indexOfSmallestAccessTime = currentCacheIndex;
			}
			
			currentCacheIndex++;
		}
	
		currentCacheIndex = cache;
		
		clock++;
		indexOfSmallestAccessTime->access_time = clock;
		indexOfSmallestAccessTime->disk_num = disk_num;
		indexOfSmallestAccessTime->block_num = block_num;
		indexOfSmallestAccessTime->valid = true;
		memcpy(indexOfSmallestAccessTime->block, buf, JBOD_BLOCK_SIZE);
	}
		
	else
	{
		/*
		 * If the requested block is not already in the cache AND the cache is not full, we linearly insert blocks into the empty cache indices.
		 * We use unfilledCurrentCacheIndex to keep track of the index of the next empty cache. 
		 */
		cacheIsEmpty = false;
		clock++;
		unfilledCurrentCacheIndex->access_time = clock;
		unfilledCurrentCacheIndex->disk_num = disk_num;
		unfilledCurrentCacheIndex->block_num = block_num;
		unfilledCurrentCacheIndex->valid = true;
		memcpy(unfilledCurrentCacheIndex->block, buf, JBOD_BLOCK_SIZE);
		numFilledCacheIndices++;
			
		/*	
		 * Checks if the cache is full, i.e., the number of filled cache indices equals the cache size.
		 * Otherwise increment the pointer for the next call of insert.
		 */
		if (numFilledCacheIndices == cache_size)
		{
			cacheIsFull = true;
		}
			
		else 
		{
			unfilledCurrentCacheIndex++;
		}
			
	}
	
  	return 1;
}


bool cache_enabled(void) {
	// Simple function to tell us if the cache is currently enabled.
	if (cache_size >= 2){
	 	return true;
	 }
	 
	else{
	 	return false;
	 }
}


void cache_print_hit_rate(void) {
  fprintf(stderr, "Hit rate: %5.1f%%\n", 100 * (float) num_hits / num_queries);
}
