/*
    Copyright (C) 2015 Datto Inc.

    This file is part of dattobd.

    This program is free software; you can redistribute it and/or modify it
    under the terms of the GNU General Public License version 2 as published
    by the Free Software Foundation.
*/

#define _FILE_OFFSET_BITS 64
#define __USE_LARGEFILE64

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <stdint.h>
#include <unistd.h>

#define COW_META_HEADER_SIZE 4096
#define COW_BLOCK_LOG_SIZE 12
#define COW_BLOCK_SIZE (1 << COW_BLOCK_LOG_SIZE)
#define INDEX_BUFFER_SIZE 4096 * 2

#define MIN(x, y) (((x) < (y)) ? (x) : (y))

typedef unsigned long long sector_t;

static void print_help(char* progname, int status){
	printf("Usage: %s <snapshot device> <cow file> <image file>\n", progname);
	exit(status);
}

static int copy_block(FILE *snap, FILE *img, sector_t block){
	char buf[COW_BLOCK_SIZE];
	int ret;
	size_t bytes;

	bytes = pread(fileno(snap), buf, COW_BLOCK_SIZE, block * COW_BLOCK_SIZE);
	if(bytes != COW_BLOCK_SIZE){
		ret = errno;
		errno = 0;
		printf("error reading data block from snapshot\n");
		goto error;
	}

	bytes = pwrite(fileno(img), buf, COW_BLOCK_SIZE, block * COW_BLOCK_SIZE);
	if(bytes != COW_BLOCK_SIZE){
		ret = errno;
		errno = 0;
		printf("error writing data block to output image\n");
		goto error;
	}

	return 0;

error:
	printf("error copying sector to output image\n");
	return ret;
}

int main(int argc,char *argv[]){
	int ret;
	size_t snap_size, bytes, blocks_to_read;
	sector_t total_chunks, total_blocks, i, j, blocks_done = 0, count = 0, err_count = 0;
	FILE *cow = NULL, *snap = NULL, *img = NULL;
	uint64_t *mappings = NULL;

	if(argc != 4) print_help(argv[0], EINVAL);

	//open snapshot
	snap = fopen(argv[1], "r");
	if(!snap){
		ret = errno;
		errno = 0;
		printf("error opening snapshot\n");
		goto error;
	}

	//open cow file
	cow = fopen(argv[2], "r");
	if(!cow){
		ret = errno;
		errno = 0;
		printf("error opening cow file\n");
		goto error;
	}

	//open original image
	img = fopen(argv[3], "r+");
	if(!img){
		ret = errno;
		errno = 0;
		printf("error opening image\n");
		goto error;
	}

	//get size of snapshot, xalculate other needed sizes
	fseeko(snap, 0, SEEK_END);
	snap_size = ftello(snap);
	total_blocks = (snap_size + COW_BLOCK_SIZE - 1) / COW_BLOCK_SIZE;
	total_chunks = (total_blocks + INDEX_BUFFER_SIZE - 1) / INDEX_BUFFER_SIZE;
	rewind(snap);

	printf("snapshot is %llu blocks large\n", total_blocks);

	//allocate mappings array
	mappings = malloc(INDEX_BUFFER_SIZE * sizeof(uint64_t));
	if(!mappings){
		ret = ENOMEM;
		printf("error allocating mappings\n");
		goto error;
	}

	//count number of blocks changed while performing merge
	printf("copying blocks\n");
	for(i = 0; i < total_chunks; i++){
		//read a chunk of mappings from the cow file
		blocks_to_read = MIN(INDEX_BUFFER_SIZE, total_blocks - blocks_done);

		bytes = pread(fileno(cow), mappings, blocks_to_read * sizeof(uint64_t), COW_META_HEADER_SIZE + (INDEX_BUFFER_SIZE * sizeof(uint64_t) * i));
		if(bytes != blocks_to_read * sizeof(uint64_t)){
			ret = errno;
			errno = 0;
			printf("error reading mappings into memory\n");
			goto error;
		}

		//copy blocks where the mapping is set
		for(j = 0; j < blocks_to_read; j++){
			if(!mappings[j]) continue;

			ret = copy_block(snap, img, (INDEX_BUFFER_SIZE * i) + j);
			if(ret) err_count++;

			count++;
		}

		blocks_done += blocks_to_read;
	}

	//print number of blocks changed
	printf("copying complete: %llu blocks changed, %llu errors\n", count, err_count);

	free(mappings);
	fclose(cow);
	fclose(snap);
	fclose(img);

	return 0;

error:
	if(mappings) free(mappings);
	if(cow) fclose(cow);
	if(snap) fclose(snap);
	if(img) fclose(img);

	return ret;
}
