#include <stdlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cache.h"

static int newid = 0;

//create cache
cache cache_init(int size, int mode) {
	cache c = (cache)malloc(sizeof(cacheObj)); //make space for cache
	c->entries = (cache_entry**)malloc(sizeof(cache_entry*)*size); //make space for cache entries
	for(int i = 0; i < size; i++)
	{
		c->entries[i] = NULL;
	}
	c->occupied = 0;
	c->mode = mode;
	c->size = size;
	return c;
}

//creates a new cache entry
cache_entry* create_entry(char* file, time_t edit_time, char* content) {
	cache_entry* c = (cache_entry*)malloc(sizeof(cache_entry)); //make space for cache entry
	c->file = malloc((sizeof(char)*strlen(file)) + 1);
	strcpy(c->file, file);
	c->edit_time = edit_time;
	c->content = content; 
	c->id = newid++;
	return c;
}

//free cache entry
void free_entry(cache_entry* c) {
	if(c == NULL)
	{
		//printf("null in free_entry\n");
		return;
	}
	free(c->file);
	free(c->content);
	free(c);
}


//add to cache, replace if necessary
void add_cache(cache c, cache_entry* entry) {
	if(c == NULL || entry == NULL)
	{
		printf("null in add_cache()\n");
		return;
	}
	//check if there's space in cache
	if(c->occupied < c->size)
	{
		//directly add to end of cache array
		//loop through cache and replace first NULL spot 
		for(int i = 0; i < c->size; i++)
		{
			if(c->entries[i] == NULL)
			{
				c->entries[i] = entry;
				c->occupied++; //increment spots occupied in cache
				return;
			}
		}	
	}
	else
	{
		int min_id = c->entries[0]->id;
		int min_index = 0;
		for(int i = 1; i < c->size; i++)
		{
			if(c->entries[i]->id < min_id) 	//find entry with smallest id
			{
				min_id = c->entries[i]->id;
				min_index = i;
			}
		}
		free_entry(c->entries[min_index]); 	//free old one
		c->entries[min_index] = entry; 		//set new one
	}
	return;
}


//return cache entry if it exists in the cache
//returns NULL if it isn't in cache
cache_entry* get(cache c, char* filename) {
	if(c->occupied == 0) //cache is empty
	{
		return NULL;
	}
	for(int i = 0; i < c->size; i++)
	{
		if(c->entries[i] != NULL)
		{
			if(strcmp(c->entries[i]->file, filename) == 0) //file is in cache
			{
				if(c->mode == 1) //LRU
				{
					c->entries[i]->id = newid++; 
				}
				return c->entries[i];
			}	
		}
	}
	return NULL; //file not found
}
