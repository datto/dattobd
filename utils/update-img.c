// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2015 Datto Inc.
 * Additional contributions by Elastio Software, Inc are Copyright (C) 2020 Elastio Software Inc.
 */

#define _FILE_OFFSET_BITS 64
#define __USE_LARGEFILE64

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>

#include "kernel-config.h"
#include "libelastio-snap.h"

#define INDEX_BUFFER_SIZE 8192

#define MIN(x, y) (((x) < (y)) ? (x) : (y))

typedef unsigned long long sector_t;

static void print_help(char* progname, int status){
	fprintf(stderr, "Usage: %s <snapshot device> <cow file> <image file>\n", progname);
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
		fprintf(stderr, "error reading data block from snapshot\n");
		goto error;
	}

	bytes = pwrite(fileno(img), buf, COW_BLOCK_SIZE, block * COW_BLOCK_SIZE);
	if(bytes != COW_BLOCK_SIZE){
		ret = errno;
		errno = 0;
		fprintf(stderr, "error writing data block to output image\n");
		goto error;
	}

	return 0;

error:
	fprintf(stderr, "error copying sector to output image\n");
	return ret;
}

static int verify_files(FILE *cow, unsigned minor){
	int ret;
	size_t bytes;
	struct cow_header ch;
	struct elastio_snap_info *info = NULL;

	//allocate a buffer for the proc data
	info = malloc(sizeof(struct elastio_snap_info));
	if(!info){
		ret = ENOMEM;
		errno = 0;
		fprintf(stderr, "error allocating memory for elastio-snap-info\n");
		goto error;
	}

	//read info from the elastio-snap driver
	ret = elastio_snap_info(minor, info);
	if(ret){
		ret = errno;
		errno = 0;
		fprintf(stderr, "error reading elastio-snap-info from driver\n");
		goto error;
	}

	//read cow header from cow file
	bytes = pread(fileno(cow), &ch, sizeof(struct cow_header), 0);
	if(bytes != sizeof(struct cow_header)){
		ret = errno;
		errno = 0;
		fprintf(stderr, "error reading cow header\n");
		goto error;
	}

	//check the cow file's magic number
	if(ch.magic != COW_MAGIC){
		ret = EINVAL;
		fprintf(stderr, "invalid magic number from cow file\n");
		goto error;
	}

	//check the uuid
	if(memcmp(ch.uuid, info->uuid, COW_UUID_SIZE) != 0){
		ret = EINVAL;
		fprintf(stderr, "cow file uuid does not match snapshot\n");
		goto error;
	}

	//check the sequence id
	if(ch.seqid != info->seqid - 1){
		ret = EINVAL;
		fprintf(stderr, "snapshot provided does not immediately follow the snapshot that created the cow file\n");
		goto error;
	}

	free(info);

	return 0;

error:
	if(info) free(info);
	return ret;
}

int main(int argc, char **argv){
	int ret;
	unsigned minor;
	size_t snap_size, bytes, blocks_to_read;
	sector_t total_chunks, total_blocks, i, j, blocks_done = 0, count = 0, err_count = 0;
	FILE *cow = NULL, *snap = NULL, *img = NULL;
	uint64_t *mappings = NULL;
	char *snap_path;
	char snap_path_buf[PATH_MAX];

	if(argc != 4) print_help(argv[0], EINVAL);

	//open snapshot
	snap = fopen(argv[1], "r");
	if(!snap){
		ret = errno;
		errno = 0;
		fprintf(stderr, "error opening snapshot\n");
		goto error;
	}

	//open cow file
	cow = fopen(argv[2], "r");
	if(!cow){
		ret = errno;
		errno = 0;
		fprintf(stderr, "error opening cow file\n");
		goto error;
	}

	//open original image
	img = fopen(argv[3], "r+");
	if(!img){
		ret = errno;
		errno = 0;
		fprintf(stderr, "error opening image\n");
		goto error;
	}

	//get the full path of the cow file
	snap_path = realpath(argv[1], snap_path_buf);
	if(!snap_path){
		ret = errno;
		errno = 0;
		fprintf(stderr, "error determining full path of snapshot\n");
		goto error;
	}

	//get the minor number of the snapshot
	ret = sscanf(snap_path, "/dev/elastio-snap%u", &minor);
	if(ret != 1){
		ret = errno;
		errno = 0;
		fprintf(stderr, "snapshot does not appear to be a elastio-snap snapshot device\n");
		goto error;
	}

	//verify all of the inputs before attempting to merge
	ret = verify_files(cow, minor);
	if(ret) goto error;

	//get size of snapshot, calculate other needed sizes
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
		fprintf(stderr, "error allocating mappings\n");
		goto error;
	}

	//count number of blocks changed while performing merge
	printf("copying blocks\n");
	for(i = 0; i < total_chunks; i++){
		//read a chunk of mappings from the cow file
		blocks_to_read = MIN(INDEX_BUFFER_SIZE, total_blocks - blocks_done);

		bytes = pread(fileno(cow), mappings, blocks_to_read * sizeof(uint64_t), COW_HEADER_SIZE + (INDEX_BUFFER_SIZE * sizeof(uint64_t) * i));
		if(bytes != blocks_to_read * sizeof(uint64_t)){
			ret = errno;
			errno = 0;
			fprintf(stderr, "error reading mappings into memory\n");
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
