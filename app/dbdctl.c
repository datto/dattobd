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

static void print_help(int status){
	printf("Usage:\n");
	printf("\tdbdctl setup-snapshot [-c <cache size>] [-f fallocate] <block device> <cow file> <minor>\n");
	printf("\tdbdctl reload-snapshot [-c <cache size>] <block device> <cow file> <minor>\n");
	printf("\tdbdctl reload-incremental [-c <cache size>] <block device> <cow file> <minor>\n");
	printf("\tdbdctl destroy <minor>\n");
	printf("\tdbdctl transition-to-incremental <minor>\n");
	printf("\tdbdctl transition-to-snapshot [-f fallocate] <cow file> <minor>\n");
	printf("\tdbdctl reconfigure [-c <cache size>] <minor>\n");
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
		if(!isdigit(*c)) goto parse_ul_error;
		c++;
	}while(*c);

	//convert to long long
	tmp = strtoll(str, NULL, 0);
	if(errno) goto parse_ul_error;

	//check boundaries
	if(tmp < 0 || tmp == LLONG_MAX){
		errno = ERANGE;
		goto parse_ul_error;
	}

	*out = (unsigned long)tmp;
	return 0;

parse_ul_error:
	*out = 0;
	return -1;
}

static int parse_ui(const char *str, unsigned int *out){
	long tmp;
	const char *c = str;

	//check that string is an integer number and has a length
	do{
		if(!isdigit(*c)) goto parse_ui_error;
		c++;
	}while(*c);

	//convert to long
	tmp = strtol(str, NULL, 0);
	if(errno) goto parse_ui_error;

	//check boundaries
	if(tmp < 0 || tmp == LONG_MAX){
		errno = ERANGE;
		goto parse_ui_error;
	}

	*out = (unsigned int)tmp;
	return 0;

parse_ui_error:
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
			if(ret) goto handle_setup_snap_error;
			break;
		case 'f':
			ret = parse_ul(optarg, &fallocated_space);
			if(ret) goto handle_setup_snap_error;
			break;
		default:
			errno = EINVAL;
			goto handle_setup_snap_error;
		}
	}

	if(argc - optind != 3){
		errno = EINVAL;
		goto handle_setup_snap_error;
	}

	bdev = argv[optind];
	cow = argv[optind + 1];

	ret = parse_ui(argv[optind + 2], &minor);
	if(ret) goto handle_setup_snap_error;

	return dattobd_setup_snapshot(minor, bdev, cow, fallocated_space, cache_size);

handle_setup_snap_error:
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
			if(ret) goto handle_reload_snap_error;
			break;
		default:
			errno = EINVAL;
			goto handle_reload_snap_error;
		}
	}

	if(argc - optind != 3){
		errno = EINVAL;
		goto handle_reload_snap_error;
	}

	bdev = argv[optind];
	cow = argv[optind + 1];

	ret = parse_ui(argv[optind + 2], &minor);
	if(ret) goto handle_reload_snap_error;

	return dattobd_reload_snapshot(minor, bdev, cow, cache_size);

handle_reload_snap_error:
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
			if(ret) goto handle_reload_inc_error;
			break;
		default:
			errno = EINVAL;
			goto handle_reload_inc_error;
		}
	}

	if(argc - optind != 3){
		errno = EINVAL;
		goto handle_reload_inc_error;
	}

	bdev = argv[optind];
	cow = argv[optind + 1];

	ret = parse_ui(argv[optind + 2], &minor);
	if(ret) goto handle_reload_inc_error;

	return dattobd_reload_incremental(minor, bdev, cow, cache_size);

handle_reload_inc_error:
	perror("error interpreting reload incremental parameters");
	print_help(-1);
	return 0;
}

static int handle_destroy(int argc, char **argv){
	int ret;
	unsigned int minor;

	if(argc != 2){
		errno = EINVAL;
		goto handle_destroy_error;
	}

	ret = parse_ui(argv[1], &minor);
	if(ret) goto handle_destroy_error;

	return dattobd_destroy(minor);

handle_destroy_error:
	perror("error interpreting destroy parameters");
	print_help(-1);
	return 0;
}

static int handle_transition_inc(int argc, char **argv){
	int ret;
	unsigned int minor;

	if(argc != 2){
		errno = EINVAL;
		goto handle_transition_inc_error;
	}

	ret = parse_ui(argv[1], &minor);
	if(ret) goto handle_transition_inc_error;

	return dattobd_transition_incremental(minor);

handle_transition_inc_error:
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
			if(ret) goto handle_transition_snap_error;
			break;
		default:
			errno = EINVAL;
			goto handle_transition_snap_error;
		}
	}

	if(argc - optind != 2){
		errno = EINVAL;
		goto handle_transition_snap_error;
	}

	cow = argv[optind];

	ret = parse_ui(argv[optind + 1], &minor);
	if(ret) goto handle_transition_snap_error;

	return dattobd_transition_snapshot(minor, cow, fallocated_space);

handle_transition_snap_error:
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
			if(ret) goto handle_reconfigure_error;
			break;
		default:
			errno = EINVAL;
			goto handle_reconfigure_error;
		}
	}

	if(argc - optind != 1){
		errno = EINVAL;
		goto handle_reconfigure_error;
	}

	ret = parse_ui(argv[optind], &minor);
	if(ret) goto handle_reconfigure_error;

	return dattobd_reconfigure(minor, cache_size);

handle_reconfigure_error:
	perror("error interpreting reconfigure parameters");
	print_help(-1);
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
	else if(!strcmp(argv[1], "help")) print_help(0);
	else print_help(-1);

	if(ret) perror("driver returned an error performing specified action. check dmesg for more info");

	return ret;
}
