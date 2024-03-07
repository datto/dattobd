#include "includes.h"

MODULE_LICENSE("GPL");

static inline void dummy(void){
	struct block_device dev;
	void* info;
	blkdev_put(&dev,info);
	return;
}