Elastio Change-Tracking Block Driver
==================

**NOTE**: This is a GPL licensed fork of the original [Datto `dattobd` driver](https://github.com/datto/dattobd).
Unless you are an Elastio customer or developer, you're almost certainly better off using the original and not this
work.

For build and install instructions, see [INSTALL.md](INSTALL.md).
For information about the on-disk .elastio file this module creates, see [STRUCTURE.md](doc/STRUCTURE.md).
For instructions regarding the `elioctl` tool, see [elioctl.8.md](doc/elioctl.8.md).
For details on the license of the software, see [LICENSING.md](LICENSING.md).

## The Linux Snapshot Problem

Linux has some basic tools for creating instant copy-on-write (COW) “snapshots” of filesystems.  The most prominent of these are LVM and device mapper (on which LVM is built).  Unfortunately, both have limitations that render them unsuitable for supporting live server snapshotting across disparate Linux environments.  Both require an unused volume to be available on the machine to track COW data. Servers, and particularly production servers, may not be preconfigured with the required spare volume.  In addition, these snapshotting systems only allow a read-only volume to be made read-write.  Taking a live backup requires unmounting your data volume, setting up a snapshot of it, mounting the snapshot, and then using a tool like `dd` or `rsync` to copy the original volume to a safe location.  Many production servers simply cannot be brought down for the time it takes to do this and afterwards all of the new data in the COW volume must eventually be merged back in to the original volume (which requires even more downtime). This is impractical and extremely hacky, to say the least.

## Elastio Block Driver (Linux Kernel Module / Driver)

The Elastio Block Driver (elastio-snap) solves the above problems and brings functionality similar to VSS on Windows to a broad range of Linux kernels.  Elastio-Snap is an open source Linux kernel module for point-in-time live snapshotting.  Elastio-Snap can be loaded onto a running Linux machine (without a reboot) and creates a COW file on the original volume representing any block device at the instant the snapshot is taken.  After the first snapshot, the driver tracks incremental changes to the block device and therefore can be used to efficiently update existing backups by copying only the blocks that have changed.  Elastio-Snap is a true live-snapshotting system that will leave your root volume running and available, without requiring a reboot.

Elastio-Snap is designed to run on any linux device from small test virtual machines to live production servers with minimal impact on I/O or CPU performance.  Since the driver works at the block layer, it supports most common filesystems including ext 2,3 and 4 and xfs (although filesystems with their own block device management systems such as ZFS and BTRFS can not be supported).  All COW data is tracked in a file on the source block device itself, eliminating the need to have a spare volume in order to snapshot.  

## Performing Incremental Backups

The primary intended use case of Elastio-Snap is for backing up live Linux systems. The general flow is to take a snapshot, copy it and move the snapshot into 'incremental' mode. Later, we can move the incremental back to snapshot mode and efficiently update the first backup we took. We can then repeat this process to continually update our backed-up image.  What follows is an example of using the driver for this purpose on a simple Ubuntu 12.04 installation with a single root volume on `/dev/sda1`. In this case, we are copying to another (larger) volume mounted at `/backups`. Other Linux distros should work similarly, with minor tweaks.

1) Install the driver and related tools. Instructions for doing this are explained in [INSTALL.md](INSTALL.md).

2) Create a snapshot:
```
	elioctl setup-snapshot /dev/sda1 /.elastio 0
```

This will create a snapshot of the root volume at `/dev/elastio-snap0` with a backing COW file at `/.elastio`. This file must exist on the volume that will be snapshotted.

3) Copy the image off the block device:
```
	dd if=/dev/elastio-snap0 of=/backups/sda1-bkp bs=1M
```

`dd` is a standard image copying tool in linux. Here it simply copies the contents of the `/dev/elastio-snap0` device to an image. Be careful when running this command as it can badly corrupt filesystems if used incorrectly. NEVER execute `dd` with the "of" parameter pointing to a volume that has important data on it. This can take a while to copy the entire volume. See the man page on `dd` for more details.

4) Put the snapshot into incremental mode:
```
	elioctl transition-to-incremental 0
```

This command requests the driver to move the snapshot (`/dev/elastio-snap0`) to incremental mode. From this point on, the driver will only track the addresses of blocks that have changed (without the data itself). This mode is less system intensive, but is important for later when we wish to update the `/backups/sda1-bkp` to reflect a later snapshot of the filesystem.

5) Continue using your system.
After the initial backup, the driver will probably be left in incremental mode the vast majority of time.


6) Move the incremental back to snapshot mode:
```
	elioctl transition-to-snapshot /.elastio1 0
```

This command requires the name of a new COW file to begin tracking changes again (here we chose `/.elastio1`). At this point the driver is finished with our `/.elastio` file we created in step 2. The `/.elastio` file now contains a list of the blocks that have changed since our initial snapshot. We will use this in the next step to update our backed up image. It is important to not use the same file name that we specified in step 2 for this command. Otherwise, we would overwrite our list of changed blocks.

7) Copy the changes:
```
	update-img /dev/elastio-snap0 /.elastio /backups/sda1-bkp
```

Here we can use the update-img tool included with the driver. It takes 3 parameters: a snapshot (`/dev/elastio-snap0`), the list of changed blocks (`/.elastio` from step 1), and an original backup image (`/backups/sda1-bkp` created in step 3). It copies the blocks listed in the block list from the new snapshot to the existing image, effectively updating the image.

8) Clean up the leftover file:
```
	rm /.elastio
```

9) Go back to step 4 and repeat:
Keep in mind it is important to specify a different COW file path for each use. If you use the same file name you will overwrite the list of changed blocks. As a result you will have to use dd to perform a full copy again instead of using the faster `update-img` tool (which only copies the changed blocks).

If you wish to keep multiple versions of the image, we recommend that you copy your images a snapshotting filesystem (such as BTRFS or ZFS). You can then snapshot the images after updating them (step 3 for the full backup or 7 the differential). This will allow you to keep a history of revisions to the image.

## Driver Status

The current status of the elastio-snap driver can be read from the file `/proc/elastio-info`. This is a JSON-formatted file with 2 fields: a version number "version" and an array of "devices". Each device has the following fields:

* `minor`: The minor number of the snapshot (for identification purposes).
* `cow_file`: The path to the cow file *relative* to the mountpoint of the block device. If the device is in an unverified state, the path is presented as it was given to the driver.
* `block_device`: The block device being tracked by this device.
* `max_cache`: The maximum amount of memory that may be used to cache metadata for this device (in bytes).
* `fallocate`: The preallocated size of the cow file (in bytes). This will not be printed if the device is in the unverified state.
* `seq_id`: The sequence id of the snapshot. This number starts at 1 for new snapshots and is incremented on each transition to snapshot.
* `uuid`: Uniquely identifies a series of snapshots. It is not changed on state transition.
* `error`: This field will only be present if the device has failed. It shows the linux standard error code indicating what went wrong. More specific info is printed to dmesg.
* `state`: An integer representing the current working state of the device. There are 6 possible states; for more info on these refer to [STRUCTURE.md](doc/STRUCTURE.md).
	* 0 = dormant incremental
	* 1 = dormant snapshot
	* 2 = active incremental
	* 3 = active snapshot
	* 4 = unverified incremental
	* 5 = unverified snapshot
* `nr_changed_blocks`: The number of blocks that have changed since the last snapshot.
* `version`: Version of the on-disk format of the COW header.
