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

#define COW_META_HEADER_SIZE 4096
#define COW_BLOCK_LOG_SIZE 12
#define COW_BLOCK_SIZE (1 << COW_BLOCK_LOG_SIZE)

typedef unsigned long long sector_t;

static void print_help(char* progname, int status){
	printf("Usage: %s <snapshot device> <cow file> <image file>\n", progname);
	exit(status);
}

static int copy_sector(FILE *snap, FILE *img, sector_t block){
	char buf[COW_BLOCK_SIZE];
	int ret;
	
	ret = fseeko(snap, block * COW_BLOCK_SIZE, SEEK_SET);
	if(ret){
		perror("error seeking within snapshot");
		return ret;
	}
	
	ret = (int)fread(buf, COW_BLOCK_SIZE, 1, snap);
	if(ret != 1){
		printf("error reading from sector %llu of snapshot\n", block);
		return ret;
	}
	
	ret = fseeko(img, block * COW_BLOCK_SIZE, SEEK_SET);
	if(ret){
		printf("error seeking to sector %llu of image\n", block);
		return ret;
	}
	
	ret = (int)fwrite(buf, COW_BLOCK_SIZE, 1, img);
	if(ret != 1){
		printf("error writing to sector %llu of image\n", block);
		return ret;
	}
	
	return 0;
}

int main(int argc,char *argv[]){
	int ret;
	sector_t blocks, bytes, i, count = 0, err_count = 0;
	FILE *cow = NULL, *snap = NULL, *img = NULL;
	uint64_t *mappings = NULL;
	
	if(argc != 4) print_help(argv[0], -1);
	
	//open snapshot
	snap = fopen(argv[1], "r");
	if(!snap){
		perror("error opening snapshot");
		goto out;
	}
	
	//open cow file
	cow = fopen(argv[2], "r");
	if(!cow){
		perror("error opening cow file");
		goto out;
	}
	
	//open original image
	img = fopen(argv[3], "r+");
	if(!img){
		perror("error opening image");
		goto out;
	}
	
	//get size of snapshot
	fseeko(snap, 0, SEEK_END);
	bytes = ftello(snap);
	blocks = bytes / 4096;
	rewind(snap);
	
	printf("snapshot is %llu blocks large, allocating mappings\n", blocks);
	
	//allocate mappings
	mappings = malloc(blocks*sizeof(uint64_t));
	if(!mappings){
		printf("error allocating mappings\n");
		goto out;
	}
	
	//read mappings into buffer
	//mappings may be sparse, read length may be smaller than actual length
	printf("reading mappings into memory\n");
	fseeko(cow, COW_META_HEADER_SIZE, SEEK_SET);
	fread(mappings, blocks*sizeof(uint64_t), 1, cow);
	
	//count number of blocks changed while performing merge
	printf("copying blocks\n");
	for(i=0; i<blocks; i++){
		if(!mappings[i]) continue;
		
		ret = copy_sector(snap, img, i);
		if(ret){
			printf("error copying block %llu\n", i);
			err_count++;
		}
		count++;
	}
	
	//print number of blocks changed
	printf("copying complete: %llu blocks changed, %llu errors\n", count, err_count);
	
out:
	if(mappings) free(mappings);
	if(cow) fclose(cow);
	if(snap) fclose(snap);
	if(img) fclose(img);
	
	return 0;
}
