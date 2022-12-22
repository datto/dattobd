// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2015 Datto Inc.
 * Additional contributions by Elastio Software, Inc are Copyright (C) 2020 Elastio Software Inc.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <ctype.h>

#include "libelastio-snap.h"

#define UNVERIFIED_STATE 4

static void print_help(int status){
	printf("Usage:\n");
	printf("\telioctl setup-snapshot [-c <cache size>] [-f fallocate] [-i (ignore snap errors)] <block device> <cow file> <minor>\n");
	printf("\telioctl reload-snapshot [-c <cache size>] [-i (ignore snap errors)] <block device> <cow file> <minor>\n");
	printf("\telioctl reload-incremental [-c <cache size>] [-i (ignore snap errors)] <block device> <cow file> <minor>\n");
	printf("\telioctl destroy <minor>\n");
	printf("\telioctl transition-to-incremental <minor>\n");
	printf("\telioctl transition-to-snapshot [-f fallocate] <cow file> <minor>\n");
	printf("\telioctl reconfigure [-c <cache size>] <minor>\n");
	printf("\telioctl info <minor>\n");
	printf("\telioctl get-free-minor\n");
	printf("\telioctl help\n\n");
	printf("<cow file> should be specified as an absolute path.\n");
	printf("cache size should be provided in bytes, and fallocate should be provided in megabytes.\n");
	printf("note:\n");
	printf("  * if the -c or -f options are not specified for any given call, module defaults are used.\n");
	printf("  * -i allows to not propagate IO errors on snapshot read operations when the snapshot is in the failed state.\n");
	printf("    it should be specified to avoid SIGBUS on an error while reading the snapshot device as a memory-mapped file.\n");
	exit(status);
}

static int parse_ul(const char *str, unsigned long *out){
	long long tmp;
	const char *c = str;

	//check that string is an integer number and has a length
	do{
		if(!isdigit(*c)){
			errno = EINVAL;
			goto error;
		}
		c++;
	}while(*c);

	//convert to long long
	tmp = strtoll(str, NULL, 0);
	if(errno) goto error;

	//check boundaries
	if(tmp < 0 || tmp == LLONG_MAX){
		errno = ERANGE;
		goto error;
	}

	*out = (unsigned long)tmp;
	return 0;

error:
	*out = 0;
	return -1;
}

static int parse_ui(const char *str, unsigned int *out){
	long tmp;
	const char *c = str;

	//check that string is an integer number and has a length
	do{
		if(!isdigit(*c)){
			errno = EINVAL;
			goto error;
		}
		c++;
	}while(*c);

	//convert to long
	tmp = strtol(str, NULL, 0);
	if(errno) goto error;

	//check boundaries
	if(tmp < 0 || tmp == LONG_MAX){
		errno = ERANGE;
		goto error;
	}

	*out = (unsigned int)tmp;
	return 0;

error:
	*out = 0;
	return -1;
}

static int handle_setup_snap(int argc, char **argv){
	int ret, c;
	unsigned int minor;
	unsigned long cache_size = 0, fallocated_space = 0;
	bool ignore_snap_errors = false;
	char *bdev, *cow;

	//get cache size, fallocated space and ignore errors on snap dev params, if given
	while((c = getopt(argc, argv, "c:f:i")) != -1){
		switch(c){
		case 'c':
			ret = parse_ul(optarg, &cache_size);
			if(ret) goto error;
			break;
		case 'f':
			ret = parse_ul(optarg, &fallocated_space);
			if(ret) goto error;
			break;
		case 'i':
			ignore_snap_errors = true;
			break;
		default:
			errno = EINVAL;
			goto error;
		}
	}

	if(argc - optind != 3){
		errno = EINVAL;
		goto error;
	}

	bdev = argv[optind];
	cow = argv[optind + 1];

	ret = parse_ui(argv[optind + 2], &minor);
	if(ret) goto error;

	return elastio_snap_setup_snapshot(minor, bdev, cow, fallocated_space, cache_size, ignore_snap_errors);

error:
	perror("error interpreting setup snapshot parameters");
	print_help(-1);
	return 0;
}

static int handle_reload_snap(int argc, char **argv){
	int ret, c;
	unsigned int minor;
	unsigned long cache_size = 0;
	char *bdev, *cow;
	bool ignore_snap_errors = false;

	//get cache size, ignore_errors if given
	while((c = getopt(argc, argv, "c:i")) != -1){
		switch(c){
		case 'c':
			ret = parse_ul(optarg, &cache_size);
			if(ret) goto error;
			break;
		case 'i':
			ignore_snap_errors = true;
			break;
		default:
			errno = EINVAL;
			goto error;
		}
	}

	if(argc - optind != 3){
		errno = EINVAL;
		goto error;
	}

	bdev = argv[optind];
	cow = argv[optind + 1];

	ret = parse_ui(argv[optind + 2], &minor);
	if(ret) goto error;

	return elastio_snap_reload_snapshot(minor, bdev, cow, cache_size, ignore_snap_errors);

error:
	perror("error interpreting reload snapshot parameters");
	print_help(-1);
	return 0;
}

static int handle_reload_inc(int argc, char **argv){
	int ret, c;
	unsigned int minor;
	unsigned long cache_size = 0;
	char *bdev, *cow;
	bool ignore_snap_errors = false;

	//get cache size, ignore_errors and fallocated space params, if given
	while((c = getopt(argc, argv, "c:i")) != -1){
		switch(c){
		case 'c':
			ret = parse_ul(optarg, &cache_size);
			if(ret) goto error;
			break;
		case 'i':
			ignore_snap_errors = true;
			break;
		default:
			errno = EINVAL;
			goto error;
		}
	}

	if(argc - optind != 3){
		errno = EINVAL;
		goto error;
	}

	bdev = argv[optind];
	cow = argv[optind + 1];

	ret = parse_ui(argv[optind + 2], &minor);
	if(ret) goto error;

	return elastio_snap_reload_incremental(minor, bdev, cow, cache_size, ignore_snap_errors);

error:
	perror("error interpreting reload incremental parameters");
	print_help(-1);
	return 0;
}

static int handle_destroy(int argc, char **argv){
	int ret;
	unsigned int minor;

	if(argc != 2){
		errno = EINVAL;
		goto error;
	}

	ret = parse_ui(argv[1], &minor);
	if(ret) goto error;

	return elastio_snap_destroy(minor);

error:
	perror("error interpreting destroy parameters");
	print_help(-1);
	return 0;
}

static int handle_transition_inc(int argc, char **argv){
	int ret;
	unsigned int minor;

	if(argc != 2){
		errno = EINVAL;
		goto error;
	}

	ret = parse_ui(argv[1], &minor);
	if(ret) goto error;

	return elastio_snap_transition_incremental(minor);

error:
	perror("error interpreting transition to incremental parameters");
	print_help(-1);
	return 0;
}

static int handle_transition_snap(int argc, char **argv){
	int ret, c;
	unsigned int minor;
	unsigned long fallocated_space = 0;
	char *cow;

	//get fallocated space param, if given
	while((c = getopt(argc, argv, "f:")) != -1){
		switch(c){
		case 'f':
			ret = parse_ul(optarg, &fallocated_space);
			if(ret) goto error;
			break;
		default:
			errno = EINVAL;
			goto error;
		}
	}

	if(argc - optind != 2){
		errno = EINVAL;
		goto error;
	}

	cow = argv[optind];

	ret = parse_ui(argv[optind + 1], &minor);
	if(ret) goto error;

	return elastio_snap_transition_snapshot(minor, cow, fallocated_space);

error:
	perror("error interpreting transition to snapshot parameters");
	print_help(-1);
	return 0;
}

static int handle_reconfigure(int argc, char **argv){
	int ret, c;
	unsigned int minor;
	unsigned long cache_size = 0;

	//get cache size and fallocated space params, if given
	while((c = getopt(argc, argv, "c:")) != -1){
		switch(c){
		case 'c':
			ret = parse_ul(optarg, &cache_size);
			if(ret) goto error;
			break;
		default:
			errno = EINVAL;
			goto error;
		}
	}

	if(argc - optind != 1){
		errno = EINVAL;
		goto error;
	}

	ret = parse_ui(argv[optind], &minor);
	if(ret) goto error;

	return elastio_snap_reconfigure(minor, cache_size);

error:
	perror("error interpreting reconfigure parameters");
	print_help(-1);
	return 0;
}

static int handle_info(int argc, char **argv){
	int ret;
	int i;
	unsigned int minor;
	struct elastio_snap_info info;

	if(argc != 2){
		errno = EINVAL;
		goto error;
	}

	ret = parse_ui(argv[1], &minor);
	if(ret) goto error;

	ret = elastio_snap_info(minor, &info);
	if(ret == 0) {
		printf("{\n");
		printf("\t\"minor\": %u,\n", info.minor);
		printf("\t\"cow_file\": \"%s\",\n", info.cow);
		printf("\t\"block_device\": \"%s\",\n", info.bdev);
		printf("\t\"max_cache\": %lu,\n", info.cache_size);

		if((info.state & UNVERIFIED_STATE) == 0) {
			printf("\t\"fallocate\": %llu,\n", info.falloc_size);

			printf("\t\"seq_id\": %llu,\n", info.seqid);

			printf("\t\"uuid\": \"");
			for(i = 0; i < COW_UUID_SIZE; i++) {
				printf("%02x", (unsigned char)info.uuid[i]);
			}
			printf("\",\n");

			if(info.version > COW_VERSION_0) {
				printf("\t\"version\": %llu,\n", info.version);
				printf("\t\"nr_changed_blocks\": %llu,\n", info.nr_changed_blocks);
			}
		}

		if(info.error) printf("\t\"error\": %d,\n", info.error);

		printf("\t\"state\": %lu,\n", info.state);
		printf("\t\"ignore_snap_errors\": %i\n", info.ignore_snap_errors);
		printf("}\n");
	}

	return ret;

error:
	perror("error interpreting info parameters");
	print_help(-1);
	return 0;
}

static int handle_get_free_minor(int argc){
	int minor;

	if(argc != 1){
		errno = EINVAL;
		goto error;
	}

	minor = elastio_snap_get_free_minor();
	if(minor < 0) {
		return minor;
	}

	printf("%i\n", minor);

	return 0;

error:
	perror("error interpreting get_free_minor parameters");
	print_help(-1);
	return 0;
}


int main(int argc, char **argv){
	int ret = 0;

	//check argc
	if(argc < 2) print_help(-1);

	if(access("/dev/elastio-snap-ctl", F_OK) != 0){
		errno = EINVAL;
		perror("driver does not appear to be loaded");
		return -1;
	}

	//route to appropriate handler or print help
	if(!strcmp(argv[1], "setup-snapshot")) ret = handle_setup_snap(argc - 1, argv + 1);
	else if(!strcmp(argv[1], "reload-snapshot")) ret = handle_reload_snap(argc - 1, argv + 1);
	else if(!strcmp(argv[1], "reload-incremental")) ret = handle_reload_inc(argc - 1, argv + 1);
	else if(!strcmp(argv[1], "destroy")) ret = handle_destroy(argc - 1, argv + 1);
	else if(!strcmp(argv[1], "transition-to-incremental")) ret = handle_transition_inc(argc - 1, argv + 1);
	else if(!strcmp(argv[1], "transition-to-snapshot")) ret = handle_transition_snap(argc - 1, argv + 1);
	else if(!strcmp(argv[1], "reconfigure")) ret = handle_reconfigure(argc - 1, argv + 1);
	else if(!strcmp(argv[1], "info")) ret = handle_info(argc - 1, argv + 1);
	else if(!strcmp(argv[1], "get-free-minor")) ret = handle_get_free_minor(argc - 1);
	else if(!strcmp(argv[1], "help")) print_help(0);
	else print_help(-1);

	if(ret) perror("driver returned an error performing specified action. check dmesg for more info");

	return ret;
}
