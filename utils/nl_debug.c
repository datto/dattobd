// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2023 Elastio Software Inc.
 *
 */


#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <linux/netlink.h>
#include <unistd.h>
#include <stdint.h>
#include <signal.h>
#include <fcntl.h>

#include "nl_debug.h"

enum event_kind_t {
	KIND_EVENT_GENERIC,
	KIND_EVENT_BIO,
	KIND_EVENT_COW
};

struct event_desc {
	enum msg_type_t event;
	enum event_kind_t kind;
	const char *desc;
};

#define MSG_SIZE 256

#define CNRM  "\x1B[0m"
#define CRED  "\x1B[31m"
#define CGRN  "\x1B[32m"
#define CYEL  "\x1B[33m"
#define CBLU  "\x1B[34m"
#define CMAG  "\x1B[35m"
#define CCYN  "\x1B[36m"
#define CWHT  "\x1B[37m"
#define CRESET "\033[0m"

#define CLIENT_ADDR "127.0.0.1"
#define CLIENT_PORT 20794

struct event_desc event_text_desc[] = {
		{ EVENT_DRIVER_INIT, KIND_EVENT_GENERIC, TO_STR(EVENT_DRIVER_INIT) },
		{ EVENT_DRIVER_DEINIT, KIND_EVENT_GENERIC, TO_STR(EVENT_DRIVER_DEINIT) },
		{ EVENT_DRIVER_ERROR, KIND_EVENT_GENERIC, TO_STR(EVENT_DRIVER_ERROR) },
		{ EVENT_SETUP_SNAPSHOT, KIND_EVENT_GENERIC, TO_STR(EVENT_SETUP_SNAPSHOT) },
		{ EVENT_SETUP_UNVERIFIED_SNAP, KIND_EVENT_GENERIC, TO_STR(EVENT_SETUP_UNVERIFIED_SNAP) },
		{ EVENT_SETUP_UNVERIFIED_INC, KIND_EVENT_GENERIC, TO_STR(EVENT_SETUP_UNVERIFIED_INC) },
		{ EVENT_TRANSITION_INC, KIND_EVENT_GENERIC, TO_STR(EVENT_TRANSITION_INC) },
		{ EVENT_TRANSITION_SNAP, KIND_EVENT_GENERIC, TO_STR(EVENT_TRANSITION_SNAP) },
		{ EVENT_TRANSITION_DORMANT, KIND_EVENT_GENERIC, TO_STR(EVENT_TRANSITION_DORMANT) },
		{ EVENT_TRANSITION_ACTIVE, KIND_EVENT_GENERIC, TO_STR(EVENT_TRANSITION_ACTIVE) },
		{ EVENT_TRACING_STARTED, KIND_EVENT_GENERIC, TO_STR(EVENT_TRACING_STARTED) },
		{ EVENT_TRACING_FINISHED, KIND_EVENT_GENERIC, TO_STR(EVENT_TRACING_FINISHED) },
		{ EVENT_BIO_INCOMING_SNAP_MRF, KIND_EVENT_BIO, TO_STR(EVENT_BIO_INCOMING_SNAP_MRF) },
		{ EVENT_BIO_INCOMING_TRACING_MRF, KIND_EVENT_BIO, TO_STR(EVENT_BIO_INCOMING_TRACING_MRF) },
		{ EVENT_BIO_CALL_ORIG, KIND_EVENT_BIO, TO_STR(EVENT_BIO_CALL_ORIG) },
		{ EVENT_BIO_SNAP, KIND_EVENT_BIO, TO_STR(EVENT_BIO_SNAP) },
		{ EVENT_BIO_INC, KIND_EVENT_BIO, TO_STR(EVENT_BIO_INC) },
		{ EVENT_BIO_CLONED, KIND_EVENT_BIO, TO_STR(EVENT_BIO_CLONED) },
		{ EVENT_BIO_READ_COMPLETE, KIND_EVENT_BIO, TO_STR(EVENT_BIO_READ_COMPLETE) },
		{ EVENT_BIO_QUEUED, KIND_EVENT_BIO, TO_STR(EVENT_BIO_QUEUED) },
		{ EVENT_BIO_RELEASED, KIND_EVENT_BIO, TO_STR(EVENT_BIO_RELEASED) },
		{ EVENT_BIO_HANDLE_READ_BASE, KIND_EVENT_BIO, TO_STR(EVENT_BIO_HANDLE_READ_BASE) },
		{ EVENT_BIO_HANDLE_READ_COW, KIND_EVENT_BIO, TO_STR(EVENT_BIO_HANDLE_READ_COW) },
		{ EVENT_BIO_HANDLE_READ_DONE, KIND_EVENT_BIO, TO_STR(EVENT_BIO_HANDLE_READ_DONE) },
		{ EVENT_BIO_HANDLE_WRITE, KIND_EVENT_BIO, TO_STR(EVENT_BIO_HANDLE_WRITE) },
		{ EVENT_BIO_HANDLE_WRITE_DONE, KIND_EVENT_BIO, TO_STR(EVENT_BIO_HANDLE_WRITE_DONE) },
		{ EVENT_BIO_FREE, KIND_EVENT_BIO, TO_STR(EVENT_BIO_FREE) },
		{ EVENT_COW_READ_MAPPING, KIND_EVENT_COW, TO_STR(EVENT_COW_READ_MAPPING) },
		{ EVENT_COW_WRITE_MAPPING, KIND_EVENT_COW, TO_STR(EVENT_COW_WRITE_MAPPING) },
		{ EVENT_COW_READ_DATA, KIND_EVENT_COW, TO_STR(EVENT_COW_READ_DATA) },
		{ EVENT_COW_WRITE_DATA, KIND_EVENT_COW, TO_STR(EVENT_COW_WRITE_DATA) }
};

int sock_fd;
int proxy_fd;
static uint64_t last_seq_num = 0;
static uint64_t seq_num_errors = 0;
static uint64_t packets_lost = 0;

static void int_handler(int val) {
	printf(CRESET "\n");
	printf("Scanning done.\n");
	printf("Sequence number errors: %lu\n", seq_num_errors);
	printf("Packets lost: %lu\n", packets_lost);
	close(sock_fd);
	exit(0);
}

static void u64_to_timespec(uint64_t ktime, struct timespec *tv)
{
	tv->tv_sec = ktime / 1000000000;
	tv->tv_nsec = ktime % 1000000000;
}

static const char *event2str(enum msg_type_t type)
{
	int i;

	for (i = 0; i < EVENT_LAST; i++)
		if (event_text_desc[i].event == type)
			return event_text_desc[i].desc;

	return NULL;
}

static bool is_generic_event(enum msg_type_t type)
{
	int i;

	for (i = 0; i < EVENT_LAST; i++)
		if (event_text_desc[i].event == type) {
			return event_text_desc[i].kind == KIND_EVENT_GENERIC;
		}

	return false;
}

static bool is_bio_event(enum msg_type_t type)
{
	int i;

	for (i = 0; i < EVENT_LAST; i++)
		if (event_text_desc[i].event == type)
			return event_text_desc[i].kind == KIND_EVENT_BIO;

	return false;
}

static bool is_cow_event(enum msg_type_t type)
{
	int i;

	for (i = 0; i < EVENT_LAST; i++)
		if (event_text_desc[i].event == type)
			return event_text_desc[i].kind == KIND_EVENT_COW;

	return false;
}

static bool is_bio_write(const struct msg_header_t *msg)
{
	return msg->params.id && msg->params.flags & 0x01;
}

void usage()
{
	printf("elastio-snap driver debugging utility\n");
	printf(	"Usage:\n"
			" -s <sector> : starting sector filter\n"
			" -e <sector> : ending sector filter\n"
			" -m <bio/cow/all> : mute output\n"
			" -c : disable coloring\n"
			" -r : show read-only IO\n"
			" -w : show write-only IO\n"
			" -h : this help message\n"
			);
}

int main(int argc, char **argv)
{
	struct sockaddr_nl user_sockaddr;
	struct sockaddr_in server_addr;
	uint64_t sector_start = 0;
	uint64_t sector_end = ~0ULL;
	bool mute_all = false;
	bool coloring = true;
	bool read_only = false;
	bool write_only = false;
	bool mute_bio_events = false;
	bool mute_cow_events = false;
	int option;

	signal(SIGINT, int_handler);

	while ((option = getopt(argc, argv, "s:e:m:crwh?")) != -1) {
		switch (option)
		{
			case 's':
				sector_start = strtol(optarg, NULL, 10);
				break;
			case 'e':
				sector_end = strtol(optarg, NULL, 10);
				break;
			case 'm':
				if (!strcmp(optarg, "all")) {
					mute_all = true;
					printf("Output muted\n");
				}
				else if (!strcmp(optarg, "cow")) {
					mute_cow_events = true;
					printf("COW events muted\n");
				}
				else if (!strcmp(optarg, "bio")) {
					mute_bio_events = true;
					printf("BIO events muted\n");
				}
				break;
			case 'c':
				coloring = false;
				break;
			case 'r':
				read_only = true;
				printf("Filtering read only bio requests\n");
				break;
			case 'w':
				printf("Filtering write only bio requests\n");
				write_only = true;
				break;
			case 'h':
			case '?':
			default:
				usage();
				return -1;
		}
	}

	if (read_only && write_only) {
		printf("Invalid filter combination\n");
		return -1;
	}

	if (sector_end < sector_start) {
		printf("Sector range is invalid\n");
		return -1;
	} else {
		if (sector_end == ~0ULL)
			printf("Filtering sector range: %lu - max\n", sector_start);
		else
			printf("Filtering sector range: %lu - %lu\n", sector_start, sector_end);
	}

	sock_fd = socket(PF_NETLINK, SOCK_RAW, NETLINK_USERSOCK);
	if (sock_fd < 0){
		printf("Error while creating netlink socket\n");
		return -1;
	}

	proxy_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (proxy_fd < 0){
		printf("Error while creating proxy socket\n");
		return -1;
	}

	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(CLIENT_PORT);
	server_addr.sin_addr.s_addr = inet_addr(CLIENT_ADDR);

	memset(&user_sockaddr, 0, sizeof(user_sockaddr));
	user_sockaddr.nl_family = AF_NETLINK;
	user_sockaddr.nl_groups = NL_MCAST_GROUP;
	user_sockaddr.nl_pid = getpid();

	int ret = bind(sock_fd, (struct sockaddr*)&user_sockaddr, sizeof(user_sockaddr));
	if (ret) {
		perror("Couldn't bind the socket");
		return -1;
	}

	int flags = fcntl(sock_fd, F_GETFL);
	fcntl(sock_fd, F_SETFL, flags | O_NONBLOCK);

	int rcvbuf = 500 * 1024 * 1024;
	if (setsockopt(sock_fd, SOL_SOCKET, SO_RCVBUFFORCE,
				&rcvbuf, sizeof rcvbuf)) {
		printf("Couldn't update socket rx buf\n");
		// also consider this:
		// echo 500000000 > /proc/sys/net/core/rmem_max
		// echo 500000000 > /proc/sys/net/core/rmem_default
	}

#define MAX_MSGS	5
#define TIMEOUT 300000

	while (true) {
		int i;
		int ret;
		struct iovec iov[MAX_MSGS];
		struct mmsghdr msgs[MAX_MSGS];
		struct timespec timeout;
		struct nlmsghdr *nl_msghdr[MAX_MSGS];
		struct timespec tspec;

		memset(msgs, 0, sizeof(msgs));

		for (i = 0; i < MAX_MSGS; i++) {
			nl_msghdr[i] = (struct nlmsghdr *) malloc(NLMSG_SPACE(MSG_SIZE));
			memset(nl_msghdr[i], 0, NLMSG_SPACE(MSG_SIZE));

			iov[i].iov_base = (void*) nl_msghdr[i];
			iov[i].iov_len = NLMSG_SPACE(MSG_SIZE);

			msgs[i].msg_hdr.msg_iov = &iov[i];
			msgs[i].msg_hdr.msg_iovlen = 1;
		}

		timeout.tv_sec = 0;
		timeout.tv_nsec = 500000;

		ret = recvmmsg(sock_fd, msgs, MAX_MSGS, 0, &timeout);

		if (ret == -1)
			goto out;

		for (i = 0; i < ret; i++) {
			struct msg_header_t *msg = (struct msg_header_t *)NLMSG_DATA(nl_msghdr[i]);

			if (mute_all)
				goto skip_print;

			if (is_cow_event(msg->type) && mute_cow_events)
				goto skip_print;

			if (is_bio_event(msg->type) && mute_bio_events)
				goto skip_print;

			if (is_bio_event(msg->type) && msg->params.id &&
					(msg->params.sector < sector_start || msg->params.sector > sector_end))
				goto skip_print;

			if (is_bio_write(msg) && read_only)
				goto skip_print;

			if (!is_bio_write(msg) && write_only)
				goto skip_print;

			if (coloring) {
				if (is_generic_event(msg->type)) {
					if (msg->type == EVENT_DRIVER_ERROR)
						printf(CRED);
					else
						printf(CYEL);
				}
				else if (is_bio_event(msg->type))
					printf(CCYN);
				else if (is_cow_event(msg->type))
					printf(CMAG);
			}

			u64_to_timespec(msg->timestamp, &tspec);
			printf("[%6lu] [%7ld:%9ld] %32.32s [%2d] ", msg->seq_num, tspec.tv_sec, tspec.tv_nsec, event2str(msg->type), msg->type);
			printf("%32.32s(), line %4d", msg->source.func, msg->source.line);

			if (msg->params.id) {
				printf(", bio ID: %16lx, R/W: %2.2s, sector: %10lu, size: %10d", msg->params.id, is_bio_write(msg) ? "W" : "R", msg->params.sector, msg->params.size);
			}

			printf(", priv1: %10ld, priv2: %10ld", msg->params.priv1, msg->params.priv2);

			if (coloring)
				printf(CRESET);

			printf("\n");

skip_print:
			if(sendto(proxy_fd, msg, msgs[i].msg_len, 0,
						(struct sockaddr*)&server_addr, sizeof(server_addr)) < 0){
				printf("Unable to send message\n");
				return -1;
			}

			if (msg->type == EVENT_DRIVER_DEINIT) {
				last_seq_num = 0;
				goto out;
			}

			if (!last_seq_num) {
				last_seq_num = msg->seq_num;
				goto out;
			}

			if (msg->seq_num != last_seq_num + 1) {
				packets_lost += (msg->seq_num - last_seq_num - 1);
				if (coloring)
					printf(CRED);

				printf("DATA DROPPED: last seq_num: %lu, seq_num received: %lu\n", last_seq_num, msg->seq_num);

				if (coloring)
					printf(CRESET);
				seq_num_errors++;
			}

			last_seq_num = msg->seq_num;

		}

out:
		for (i = 0; i < MAX_MSGS; i++)
			free(nl_msghdr[i]);
	}

	return 0;
}
