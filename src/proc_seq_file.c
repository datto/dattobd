#include "cow_manager.h"
#include "dattobd.h"
#include "includes.h"
#include "ioctl_handlers.h"
#include "module_control.h"
#include "snap_device.h"
#include "tracer_helper.h"

static void *dattobd_proc_start(struct seq_file *m, loff_t *pos);
static void *dattobd_proc_next(struct seq_file *m, void *v, loff_t *pos);
static void dattobd_proc_stop(struct seq_file *m, void *v);
static int dattobd_proc_show(struct seq_file *m, void *v);
static int dattobd_proc_open(struct inode *inode, struct file *filp);
static int dattobd_proc_release(struct inode *inode, struct file *file);

#ifndef HAVE_PROC_OPS
//#if LINUX_VERSION_CODE < KERNEL_VERSION(5,6,0)
static const struct file_operations dattobd_proc_fops = {
        .owner = THIS_MODULE,
        .open = dattobd_proc_open,
        .read = seq_read,
        .llseek = seq_lseek,
        .release = dattobd_proc_release,
};
#else
static const struct proc_ops dattobd_proc_fops = {
        .proc_open = dattobd_proc_open,
        .proc_read = seq_read,
        .proc_lseek = seq_lseek,
        .proc_release = dattobd_proc_release,
};
#endif

static const struct seq_operations dattobd_seq_proc_ops = {
        .start = dattobd_proc_start,
        .next = dattobd_proc_next,
        .stop = dattobd_proc_stop,
        .show = dattobd_proc_show,
};

const struct file_operations *get_proc_fops(void)
{
        return &dattobd_proc_fops;
}

static void *dattobd_proc_get_idx(loff_t pos)
{
        if (pos > highest_minor)
                return NULL;
        return &snap_devices[pos];
}

static void *dattobd_proc_start(struct seq_file *m, loff_t *pos)
{
        if (*pos == 0)
                return SEQ_START_TOKEN;
        return dattobd_proc_get_idx(*pos - 1);
}

static void *dattobd_proc_next(struct seq_file *m, void *v, loff_t *pos)
{
        void *dev = dattobd_proc_get_idx(*pos);
        ++*pos;
        return dev;
}

static void dattobd_proc_stop(struct seq_file *m, void *v)
{
}

static int dattobd_proc_show(struct seq_file *m, void *v)
{
        struct snap_device **dev_ptr = v;
        struct snap_device *dev = NULL;

        // print the header if the "pointer" really an indication to do so
        if (dev_ptr == SEQ_START_TOKEN) {
                seq_printf(m, "{\n");
                seq_printf(m, "\t\"version\": \"%s\",\n", DATTOBD_VERSION);
                seq_printf(m, "\t\"devices\": [\n");
        }

        // if the pointer is actually a device print it
        if (dev_ptr != SEQ_START_TOKEN && *dev_ptr != NULL) {
                int error;
                dev = *dev_ptr;

                if (dev->sd_minor != lowest_minor)
                        seq_printf(m, ",\n");
                seq_printf(m, "\t\t{\n");
                seq_printf(m, "\t\t\t\"minor\": %u,\n", dev->sd_minor);
                seq_printf(m, "\t\t\t\"cow_file\": \"%s\",\n",
                           dev->sd_cow_path);
                seq_printf(m, "\t\t\t\"full_cow_path\": \"%s\",\n",
                           dev->sd_cow_full_path);
                seq_printf(m, "\t\t\t\"block_device\": \"%s\",\n",
                           dev->sd_bdev_path);
                seq_printf(m, "\t\t\t\"max_cache\": %lu,\n",
                           (dev->sd_cache_size) ?
                                   dev->sd_cache_size :
                                   dattobd_cow_max_memory_default);

                if (!test_bit(UNVERIFIED, &dev->sd_state)) {
                        seq_printf(m, "\t\t\t\"fallocate\": %llu,\n",
                                   ((unsigned long long)dev->sd_falloc_size) *
                                           1024 * 1024);

                        if (dev->sd_cow) {
                                int i;
                                seq_printf(
                                        m, "\t\t\t\"seq_id\": %llu,\n",
                                        (unsigned long long)dev->sd_cow->seqid);

                                seq_printf(m, "\t\t\t\"uuid\": \"");
                                for (i = 0; i < COW_UUID_SIZE; i++) {
                                        seq_printf(m, "%02x",
                                                   dev->sd_cow->uuid[i]);
                                }
                                seq_printf(m, "\",\n");

                                if (dev->sd_cow->version > COW_VERSION_0) {
                                        seq_printf(m,
                                                   "\t\t\t\"version\": %llu,\n",
                                                   dev->sd_cow->version);
                                        seq_printf(
                                                m,
                                                "\t\t\t\"nr_changed_blocks\": "
                                                "%llu,\n",
                                                dev->sd_cow->nr_changed_blocks);
                                }
                        }
                }

                error = tracer_read_fail_state(dev);
                if (error)
                        seq_printf(m, "\t\t\t\"error\": %d,\n", error);

                seq_printf(m, "\t\t\t\"state\": %lu,\n", dev->sd_state);
                seq_printf(m, "\t\t\t\"resident\": %d\n", test_bit(SD_FLAG_COW_RESIDENT, &dev->sd_flags));
                seq_printf(m, "\t\t}");
        }

        // print the footer if there are no devices to print or if this device
        // has the highest minor
        if ((dev_ptr == SEQ_START_TOKEN && lowest_minor > highest_minor) ||
            (dev && dev->sd_minor == highest_minor)) {
                seq_printf(m, "\n\t]\n");
                seq_printf(m, "}\n");
        }

        return 0;
}

static int dattobd_proc_open(struct inode *inode, struct file *filp)
{
        mutex_lock(&ioctl_mutex);
        return seq_open(filp, &dattobd_seq_proc_ops);
}

static int dattobd_proc_release(struct inode *inode, struct file *file)
{
        seq_release(inode, file);
        mutex_unlock(&ioctl_mutex);
        return 0;
}
