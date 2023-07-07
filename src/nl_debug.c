// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2023 Elastio Software Inc.
 *
 */

#include "nl_debug.h"

static uint64_t seq_num = 1;
struct sock *nl_sock = NULL;
spinlock_t nl_spinlock;

static void nl_recv_msg(struct sk_buff *skb)
{
	nlmsg_free(skb);
}

int nl_send_event(enum msg_type_t type, const char *func, int line, struct params_t *params)
{
	struct sk_buff *skb;
	struct msg_header_t *msg;
	struct nlmsghdr *nlsk_mh;

	skb = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_ATOMIC);
	nlsk_mh = nlmsg_put(skb, 0, 0, NLMSG_DONE, sizeof(struct msg_header_t), 0);
	NETLINK_CB(skb).portid = 0;
	NETLINK_CB(skb).dst_group = NL_MCAST_GROUP;

	spin_lock_bh(&nl_spinlock);
	msg = nlmsg_data(nlsk_mh);
	msg->type = type;
	msg->timestamp = ktime_get();
	msg->seq_num = seq_num;
	seq_num++;

	if (func) {
		msg->source.line = line;
		strncpy(msg->source.func, func, sizeof(msg->source.func));
	}

	memcpy(&msg->params, params, sizeof(*params));

	nlmsg_multicast(nl_sock, skb, 0, NL_MCAST_GROUP, GFP_ATOMIC);
	spin_unlock_bh(&nl_spinlock);
	return 0;
}

void netlink_release(void)
{
	printk("netlink release\n");
	sock_release(nl_sock->sk_socket);
}

int netlink_init(void)
{
	struct netlink_kernel_cfg cfg = {
		.input = nl_recv_msg,
	};

	printk("netlink init\n");
	spin_lock_init(&nl_spinlock);

	nl_sock = netlink_kernel_create(&init_net, NETLINK_USERSOCK, &cfg);
	if (!nl_sock) {
		printk("netlink: error creating socket\n");
		return -ENOTSUPP;
	}

	return 0;
}
