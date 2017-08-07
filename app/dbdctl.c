/*
    Copyright (C) 2015 Datto Inc.

    This file is part of dattobd.

    This program is free software; you can redistribute it and/or modify it
    under the terms of the GNU General Public License version 2 as published
    by the Free Software Foundation.
*/

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <ctype.h>
#include "../lib/libdattobd.h"
#include "../src/dattobd.h"

static void print_help(int status){
	printf("Usage:\n");
	printf("\tdbdctl setup-snapshot [-c <cache size>] [-f fallocate] <block device> <cow file> <minor>\n");
	printf("\tdbdctl reload-snapshot [-c <cache size>] <block device> <cow file> <minor>\n");
	printf("\tdbdctl reload-incremental [-c <cache size>] <block device> <cow file> <minor>\n");
	printf("\tdbdctl destroy <minor>\n");
	printf("\tdbdctl transition-to-incremental <minor>\n");
	printf("\tdbdctl transition-to-snapshot [-f fallocate] <cow file> <minor>\n");
    printf("\tdbdctl reconfigure [-c <cache size>] <minor>\n");
    printf("\tdbdctl info [minor]\n");
	printf("\tdbdctl help\n\n");
	printf("for the reload commands, <cow file> should be specified relative to the root of <block device>\n");
	printf("cache size should be provided in bytes, and fallocate should be provided in megabytes\n");
	printf("note: if the -c or -f options are not specified for any given call, module defaults are used\n");
	exit(status);
}

static int parse_ul(const char *str, unsigned long *out){
	long long tmp;
	const char *c = str;

	//check that string is an integer number and has a length
	do{
		if(!isdigit(*c)) goto error;
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
		if(!isdigit(*c)) goto error;
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
	char *bdev, *cow;

	//get cache size and fallocated space params, if given
	while((c = getopt(argc, argv, "c:f:")) != -1){
		switch(c){
		case 'c':
			ret = parse_ul(optarg, &cache_size);
			if(ret) goto error;
			break;
		case 'f':
			ret = parse_ul(optarg, &fallocated_space);
			if(ret) goto error;
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

	return dattobd_setup_snapshot(minor, bdev, cow, fallocated_space, cache_size);

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

	if(argc - optind != 3){
		errno = EINVAL;
		goto error;
	}

	bdev = argv[optind];
	cow = argv[optind + 1];

	ret = parse_ui(argv[optind + 2], &minor);
	if(ret) goto error;

	return dattobd_reload_snapshot(minor, bdev, cow, cache_size);

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

	if(argc - optind != 3){
		errno = EINVAL;
		goto error;
	}

	bdev = argv[optind];
	cow = argv[optind + 1];

	ret = parse_ui(argv[optind + 2], &minor);
	if(ret) goto error;

	return dattobd_reload_incremental(minor, bdev, cow, cache_size);

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

	return dattobd_destroy(minor);

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

	return dattobd_transition_incremental(minor);

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

	//get cache size and fallocated space params, if given
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

	return dattobd_transition_snapshot(minor, cow, fallocated_space);

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

	return dattobd_reconfigure(minor, cache_size);

error:
	perror("error interpreting reconfigure parameters");
	print_help(-1);
	return 0;
}

static void print_info(struct dattobd_info *infoitem){
    int i;
    printf("minor: %d\n", infoitem->minor);
    printf("state: %lu\n", infoitem->state);
    printf("error: %d\n", infoitem->error);
    printf("cache_size: %lu\n", infoitem->cache_size);
    printf("falloc_size: %llu\n", infoitem->falloc_size);
    printf("seqid: %llu\n", infoitem->seqid);
    printf("uuid: ");
    for(i = 0; i < COW_UUID_SIZE; i++)
        printf("%02x", (unsigned int)(infoitem->uuid[i] & 0xff));
    printf("\n");
    printf("cow: %s\n", infoitem->cow);
    printf("bdev: %s\n", infoitem->bdev);
    printf("\n");
}

static int handle_active_devices(void){
    int max = 255;
    int bufsz = sizeof(struct dattobd_active_device_info) + (max * sizeof(struct dattobd_info));
    int ret;
    int lp;
    struct dattobd_info *infoitem;
    struct dattobd_active_device_info *adi;

    adi = malloc(bufsz);
    memset(adi, 0, bufsz);
    adi->count = max; // set max we have allocated memory for.
    ret = dattobd_active_device_info(adi);
    if (ret) goto error;
    // print version first.
    printf("version [%s]\n\n", adi->version_string);

    printf("count returned: %d\n", adi->count);
    // now dump out the contents.
    for (lp = 0; lp < adi->count; lp++)
    {
        int i;
        infoitem = &(adi->info[lp]);
        print_info(infoitem);
    }
    goto end;

    error:
        perror("error from dattobd_active_device_info");
    end:
        if (adi)
            free(adi);
        return 0;
}

static int handle_info(int argc, char **argv){
    int ret;
    unsigned int minor;
    struct dattobd_info info;

    if(argc != 2){
        return handle_active_devices();
    }

    ret = parse_ui(argv[1], &minor);
    if(ret) goto error;

    ret = dattobd_info(minor, &info);
    if (ret) goto error_info;
    print_info(&info);
    return 0;

    error:
        perror("error interpreting info parameters");
        print_help(-1);
        return 0;
    error_info:
        perror("error from dattobd_info");
        return 0;
}


int main(int argc, char **argv){
	int ret = 0;

	//check argc
	if(argc < 2) print_help(-1);

	if(access("/dev/datto-ctl", F_OK) != 0){
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
	else if(!strcmp(argv[1], "help")) print_help(0);
	else print_help(-1);

	if(ret) perror("driver returned an error performing specified action. check dmesg for more info");

	return ret;
}
