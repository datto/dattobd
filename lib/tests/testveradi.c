#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include "libdattobd.h"

int main(int num, char *opts[])
{
    printf("Testing get active devices call.\n");
    struct dattobd_active_device_info *adi;
    // we offer space for a max of 255
    int max = 255;
    int bufsz = sizeof(struct dattobd_active_device_info) + ((max - 1) * sizeof(struct dattobd_info));
    adi = malloc(bufsz);
    memset(adi, 0, bufsz);
    adi->count = max; // set max we have allocated memory for.
    int ret = dattobd_active_device_info(adi);
    printf("return from active device info call: %d\n", ret);
    if (ret == 0)
    {
        // print version first.
        printf("version [%s]\n\n", adi->version_string);

        int lp;
        struct dattobd_info *infoitem;

        printf("count returned: %d\n", adi->count);
        // now dump out the contents.
        for (lp = 0; lp < adi->count; lp++)
        {
            int i;
            infoitem = &(adi->first[lp]);
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
    }
    else
        printf("errno: %d\n", errno);

    free(adi);


    return 0;
}
