// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2023 Elastio Software Inc.
 *
 */

#ifdef KERNEL_MODULE
#include <net/sock.h>
#include <linux/netlink.h>
#include <linux/skbuff.h>
#endif

#include "kernel-config.h"
#include "elastio-snap.h"

#define NL_MCAST_GROUP 1

enum msg_type_t {
	EVENT_DRIVER_INIT,
	EVENT_DRIVER_DEINIT,
	EVENT_DRIVER_ERROR,
	EVENT_SETUP_SNAPSHOT,
	EVENT_SETUP_UNVERIFIED_SNAP,
	EVENT_SETUP_UNVERIFIED_INC,
	EVENT_TRANSITION_INC,
	EVENT_TRANSITION_SNAP,
	EVENT_TRANSITION_DORMANT,
	EVENT_TRANSITION_ACTIVE,
	EVENT_TRACING_STARTED,
	EVENT_TRACING_FINISHED,
	EVENT_BIO_INCOMING_TRACING_MRF,
	EVENT_BIO_INCOMING_SNAP_MRF,
	EVENT_BIO_CALL_ORIG,
	EVENT_BIO_SNAP,
	EVENT_BIO_INC,
	EVENT_BIO_CLONED,
	EVENT_BIO_READ_COMPLETE,
	EVENT_BIO_QUEUED,   // cloned bio enqueued for the cow thread
	EVENT_BIO_RELEASED, // parent bio released
	EVENT_BIO_HANDLE_READ_BASE,
	EVENT_BIO_HANDLE_READ_COW,
	EVENT_BIO_HANDLE_READ_DONE,
	EVENT_BIO_HANDLE_WRITE,
	EVENT_BIO_HANDLE_WRITE_DONE,
	EVENT_BIO_FREE,
	EVENT_COW_READ_MAPPING,
	EVENT_COW_WRITE_MAPPING,
	EVENT_COW_READ_DATA,
	EVENT_COW_WRITE_DATA,
	EVENT_LAST
};

struct params_t {
	uint64_t id;
	uint32_t size; // in sectors
	uint64_t sector;
	uint8_t flags;
	uint64_t priv1;
	uint64_t priv2;
} __attribute__((packed));

struct code_info_t {
	char func[32];
	uint16_t line;
} __attribute__((packed));

struct msg_header_t {
	uint8_t type;
	uint64_t seq_num;
	uint64_t timestamp;
	struct params_t params;
	struct code_info_t source;
} __attribute__((packed));

#define TO_STR(_type) #_type

#define trace_event_bio(_type, _bio, _priv) \
({ 											\
	struct params_t params = { 0 };			\
											\
	if (_bio) { 							\
		params.id = (uint64_t)(_bio); 		\
		params.size = bio_size(_bio); 		\
		params.flags = bio_data_dir(_bio);	\
		params.sector = bio_sector(_bio); 	\
	} 										\
											\
	params.priv1 = (_priv); 					\
	params.priv2 = 0; 					\
	nl_send_event(_type, __func__, __LINE__, &params); \
})

#define trace_event_generic(_type, _priv) 	\
({ 											\
	struct params_t params = { 0 }; 		\
											\
	params.priv1 = (_priv); 				\
	params.priv2 = 0; 						\
	nl_send_event(_type, __func__, __LINE__, &params); \
})

#define trace_event_cow(_type, _priv1, _priv2)	\
({ 												\
	struct params_t params = { 0 }; 			\
												\
	params.priv1 = (_priv1); 					\
	params.priv2 = (_priv2); 					\
	nl_send_event(_type, __func__, __LINE__, &params); \
})

int nl_send_event(enum msg_type_t type, const char *func, int line, struct params_t *params);
void netlink_release(void);
int netlink_init(void);
