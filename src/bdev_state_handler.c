#include "bdev_state_handler.h"
#include <linux/version.h>
#include "snap_device.h"

/**
 * auto_transition_dormant() - Transitions an active snapshot to dormant.
 *
 * @minor: the device's minor number.
 * @snap_devices: the array of snap devices.
 */
static void auto_transition_dormant(unsigned int minor, snap_device_array snap_devices)
{
        LOG_DEBUG("ENTER %s minor: %d", __func__, minor);

        mutex_lock(&ioctl_mutex);
        __tracer_active_to_dormant(snap_devices[minor]);
        mutex_unlock(&ioctl_mutex);
        
        LOG_DEBUG("EXIT %s", __func__);
}

/**
 * auto_transition_active() - Transitions a device to an active state
 *                            whether snapshot or incremental.
 *
 * @minor: the device's minor number.
 * @dir_name: the user-space supplied directory name of the mount.
 * @snap_devices: the array of snap devices.
 */
static void auto_transition_active(unsigned int minor, const char *dir_name, snap_device_array_mut snap_devices)
{
        struct snap_device *dev = snap_devices[minor];

        LOG_DEBUG("ENTER %s minor: %d", __func__, minor);
        mutex_lock(&ioctl_mutex);

        if (test_bit(UNVERIFIED, &dev->sd_state)) {
                if (test_bit(SNAPSHOT, &dev->sd_state))
                        __tracer_unverified_snap_to_active(dev, dir_name, snap_devices);
                else
                        __tracer_unverified_inc_to_active(dev, dir_name, snap_devices);
        } else
                __tracer_dormant_to_active(dev, dir_name);

        mutex_unlock(&ioctl_mutex);

        LOG_DEBUG("EXIT %s", __func__);
}

/**
 * __handle_bdev_mount_nowrite() - Transitions a device to a dormant state
 *                                 when it is unmounted.
 *
 * @mnt: The &struct vfsmount object pointer.
 * @idx_out: Output the minor device number of the transitioned device.
 * @snap_devices: the array of snap devices.
 *
 * Return:
 * * 0 - success
 * * !0 - errno indicating the error
 */
static int __handle_bdev_mount_nowrite(const struct vfsmount *mnt,
                                unsigned int *idx_out, snap_device_array snap_devices)
{
        int ret;
        unsigned int i;
        struct snap_device *dev;
        tracer_for_each(dev, i)
        {
                if(!dev) continue;

                if (!test_bit(ACTIVE, &dev->sd_state) ||
                    tracer_read_fail_state(dev) ||
                    !dev->sd_base_dev ||
                    dev->sd_base_dev->bdev != mnt->mnt_sb->s_bdev)
                        continue;

                if (dev->sd_cow && dev->sd_cow->dfilp && mnt == dev->sd_cow->dfilp->mnt) {
                        LOG_DEBUG("block device umount detected for device %d",
                                  i);
                        auto_transition_dormant(i, snap_devices);

                        ret = 0;
                        goto out;
                }
                
        }
        i = 0;
        ret = -ENODEV;
        LOG_DEBUG("block device umount has not been detected for device");
out:
        *idx_out = i;
        return ret;
}

/**
 * __handle_bdev_mount_writable() - Transitions a dormant device to active
 *                                  on mount, if one exists.
 *
 * @dir_name: the user-space suplied directory name of the mount.
 * @bdev: The &struct block_device that stores the COW data.
 * @idx_out: Output the minor device number of the transitioned device.
 * @snap_devices: the array of snap devices.
 *
 * Return:
 * * 0 - success
 * * !0 - errno indicating the error
 */
static int __handle_bdev_mount_writable(const char *dir_name,
                                 const struct block_device *bdev,
                                 unsigned int *idx_out, snap_device_array_mut snap_devices)
{
        int ret;
        unsigned int i;
        struct snap_device *dev;
        struct bdev_wrapper *cur_bdev;

        LOG_DEBUG("ENTER %s", __func__);
        tracer_for_each(dev, i)
        {
                if(!dev) continue;

                if (test_bit(ACTIVE, &dev->sd_state) ||
                    tracer_read_fail_state(dev)) {
                        if(test_bit(ACTIVE, &dev->sd_state))
                        {
                                LOG_DEBUG("dev IS ACTIVE %d", dev->sd_minor);
                        }
                        continue;
                }

                if (test_bit(UNVERIFIED, &dev->sd_state)) {
                        // get the block device for the unverified tracer we are
                        // looking into
                        cur_bdev = dattobd_blkdev_by_path(dev->sd_bdev_path,
                                                      FMODE_READ, NULL);
                        if (IS_ERR(cur_bdev)) {
                                cur_bdev = NULL;
                                continue;
                        }

                        // if the tracer's block device exists and matches the
                        // one being mounted perform transition
                        if (cur_bdev->bdev == bdev) {
                                LOG_DEBUG("block device mount detected for "
                                          "unverified device %d",
                                          i);
                                auto_transition_active(i, dir_name, snap_devices);
                                dattobd_blkdev_put(cur_bdev);

                                ret = 0;
                                goto out;
                        }

                        // put the block device
                        dattobd_blkdev_put(cur_bdev);

                } else if (dev->sd_base_dev && dev->sd_base_dev->bdev == bdev) {
                        LOG_DEBUG(
                                "block device mount detected for dormant device %d",
                                i);
                        auto_transition_active(i, dir_name, snap_devices);

                        ret = 0;
                        goto out;
                }
        }
        i = 0;
        ret = -ENODEV;

out:
        LOG_DEBUG("EXIT %s", __func__);
        *idx_out = i;
        return ret;
}

/**
 * handle_bdev_mount_event() - A common impl used to handle a mount event.
 *
 * @dir_name: the user-space supplied directory name of the mount.
 * @follow_flags: flags passed to the system call.cd /
 * @idx_out: Output the minor device number of the transitioned device.
 * @mount_writable: Whether the mount is writable or not.
 *
 * Return:
 * * 0 - success.
 * * !0 - errno indicating the error.
 */
int handle_bdev_mount_event(const char *dir_name, int follow_flags,
                            unsigned int *idx_out, int mount_writable)
{
        int ret = 0; 
        int lookup_flags = 0; // init_umount LOOKUP_MOUNTPOINT;
        struct path path;
        struct block_device *bdev;

        LOG_DEBUG("ENTER %s", __func__);

        if (!(follow_flags & UMOUNT_NOFOLLOW))
                lookup_flags |= LOOKUP_FOLLOW;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,5,0)
        ret = kern_path(dir_name, lookup_flags, &path);
#else
        ret = user_path_at(AT_FDCWD, dir_name, lookup_flags, &path);
#endif //LINUX_VERSION_CODE
        if (ret) {
                LOG_DEBUG("error finding path");
                goto out_nopath;
        }

        LOG_DEBUG("path->dentry: %s, path->mnt->mnt_root: %s", path.dentry->d_name.name, path.mnt->mnt_root->d_name.name);

        if (path.dentry != path.mnt->mnt_root) {
                // path specified isn't a mount point
                ret = -ENODEV;
                LOG_DEBUG("path specified isn't a mount point %s", dir_name);
        
                goto out;
        }

        bdev = path.mnt->mnt_sb->s_bdev;        
        if (!bdev) {
                LOG_DEBUG("path specified isn't mounted on a block device");
                ret = -ENODEV;
                goto out;
        }

        if (!mount_writable){
                snap_device_array snap_devices = get_snap_device_array();
                ret = __handle_bdev_mount_nowrite(path.mnt, idx_out, snap_devices);
                put_snap_device_array(snap_devices);
        }else{
                snap_device_array_mut snap_devices = get_snap_device_array_mut();
                ret = __handle_bdev_mount_writable(dir_name, bdev, idx_out, snap_devices);
                put_snap_device_array_mut(snap_devices);
        }
        if (ret) {
                // no block device found that matched an incremental
                LOG_DEBUG("no block device found that matched an incremental %s", dir_name);
                goto out;
        }

        path_put(&path);
        return ret;
out:
        path_put(&path);
out_nopath:
        *idx_out = 0;
        return ret;
}

/**
 * post_umount_check() - Checks to make sure umount succeeded and the driver
 *                       is in a good state.
 *
 * @dormant_ret: the return value from transitioning to dormant.
 * @umount_ret: the return value from the original umount call.
 * @idx: the device minor number.
 * @dir_name: the user-space supplied directory name of the mount.
 */
void post_umount_check(int dormant_ret, int umount_ret, unsigned int idx,
                       const char *dir_name)
{
        struct snap_device *dev;
        struct super_block *sb;
        snap_device_array_mut snap_devices = NULL;

        LOG_DEBUG("ENTER %s", __func__);
        // if we didn't do anything or failed, just return
        if (dormant_ret) {
                LOG_DEBUG("EXIT %s, dormant_ret", __func__);
                return;
        }

        snap_devices = get_snap_device_array_mut();

        dev = snap_devices[idx];

        // if we successfully went dormant, but the umount call failed,
        // reactivate
        if (umount_ret) {
                struct bdev_wrapper *bdev_w;
                bdev_w = dattobd_blkdev_by_path(dev->sd_bdev_path, FMODE_READ, NULL);
                if (IS_ERR_OR_NULL(bdev_w)) {
                        LOG_DEBUG("device gone, moving to error state");
                        tracer_set_fail_state(dev, -ENODEV);
                        put_snap_device_array_mut(snap_devices);
                        return;
                }

                dattobd_blkdev_put(bdev_w);

                LOG_DEBUG("umount call failed, reactivating tracer %u", idx);
                auto_transition_active(idx, dir_name, snap_devices);
                put_snap_device_array_mut(snap_devices);
                return;
        }

        put_snap_device_array_mut(snap_devices);

        // force the umount operation to complete synchronously
        task_work_flush();

        // if we went dormant, but the block device is still mounted somewhere,
        // goto fail state
        sb = dattobd_get_super(dev->sd_base_dev->bdev);
        if (sb) {
                if (!(sb->s_flags & MS_RDONLY)) {
                        LOG_ERROR(
                                -EIO,
                                "device still mounted after umounting cow file's "
                                "file-system. entering error state");
                        tracer_set_fail_state(dev, -EIO);
                        dattobd_drop_super(sb);
                        return;
                }
                dattobd_drop_super(sb);
        }

        LOG_DEBUG("EXIT %s", __func__);
}
