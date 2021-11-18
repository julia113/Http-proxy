#ifndef __CACHE__
#define __CACHE__
#include <stdlib.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
	char* file; 		
	time_t edit_time;	//time last accessed
	int id;
	char* content; 		//content of the file
}cache_entry;

typedef struct {
	cache_entry** entries; 	//array of cache entries
	int size;				//size of cache
	int mode;				//FIFO or LRU
	int occupied;
}cacheObj;

typedef cacheObj* cache;

cache cache_init(int size, int mode);
cache_entry* create_entry(char* file, time_t edit_time, char* content);
void add_cache(cache c, cache_entry* entry);
cache_entry* get(cache c, char* filename);
void free_entry(cache_entry* c);
#endif