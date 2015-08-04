Datto Block Driver
===========

For build and install instructions, see [INSTALL.md](INSTALL.md).
For information about the on-disk .datto file this module creates, see [STRUCTURE.md](doc/STRUCTURE.md).
For instructions regarding the `dbdctl` tool, see [dbdctl.8.md](doc/dbdctl.8.md).

## The Linux Snapshot Problem

Linux has some basic tools for creating “snapshots” of filesystems.  Most use a copy-on-write (COW) scheme to allow point-in-time consistent snapshots.  Currently, both LVM and device mapper (on which LVM is built) support COW snapshotting.  Unfortunately, both have limitations that render them unsuitable for supporting live server snapshotting across disparate Linux environments.

For example, LVM snapshotting requires an unused volume in order to track COW data.  Of course, servers - especially production servers - may not be pre-configured with the required spare volume.  Furthermore, LVM itself may not be available in all Linux environments.  LVM, therefore, may be useful for creating “golden” base images from which specific system variations are created.  However, LVM is virtually useless for live production backup.

## Datto Block Driver (Linux Kernel Module)

The Datto Block Driver (Dattobd) solves the above problems. Dattobd is an open source Linux kernel module for point-in-time live snapshotting.  Dattobd can be loaded onto a running Linux machine (without a reboot) and used to create an image file representing any block device at the instant the snapshot is taken.  After the first snapshot, Dattobd tracks incremental changes to the block device and can therefore efficiently update existing backups by copying only the blocks that have changed.  

With a single command, Dattobd can almost instantly create a point-in-time snapshot device representing the snapshot state.  Between snapshots, disk writes are tracked so incremental changes can be quickly and efficiently applied to an existing image file (e.g., one created from a previous snapshot) to create an up-to-date backup (for an example, see img-merge in utils/).

Dattobd is designed to run on any linux device from small test virtual machines to live production servers with minimal impact on I/O or CPU performance.  Since the driver works at the block layer, it supports most common filesystems including EXT and XFS (although filesystems with their own block device management systems such as ZFS and BTRFS can not be supported).  All COW data is tracked in a file on the source block device itself, eliminating the need to have a spare volume in order to snapshot.  

In summary, Dattobd brings functionality similar to VSS on Windows to a broad range of Linux kernels.

## Performing Incremental Backups

The primary intended use case of Dattobd is for backing up live Linux systems. The general flow is to take a snapshot, copy it and move the snapshot into 'incremental' mode. Later, we can move the incremental back to snapshot mode and efficiently update the first backup we took. We can then repeat this process to continually update our backed-up image.  What follows is an example of using the driver for this purpose on a simple Ubuntu 12.04 installation with a single root volume on `/dev/sda1`. In this case, we are copying to another (larger) volume mounted at `/backups`. Other Linux distros should work similarly, with minor tweaks.

1) Install the driver and related tools. Instructions for doing this are explained in [INSTALL.md](INSTALL.md).

2) Create a snapshot:

	dbdctl setup-snapshot /dev/sda1 /.datto 0


This will create a snapshot of the root volume at `/dev/datto0` with a backing COW file at `/.datto`. This file must exist on the volume that will be snapshotted.

3) Copy the image off the block device:

	dd if=/dev/datto0 of=/backups/sda1-bkp bs=1M


`dd` is a standard image copying tool in linux. Here it simply copies the contents of the `/dev/datto0` device to an image. Be careful when running this command as it can badly corrupt filesystems if used incorrectly. NEVER execute `dd` with the "of" parameter pointing to a volume that has important data on it. This can take a while to copy the entire volume. See the man page on `dd` for more details.

4) Put the snapshot into incremental mode:

	dbdctl transition-to-incremental 0


This command requests the driver to move the snapshot (`/dev/datto0`) to incremental mode. From this point on, the driver will only track the addresses of blocks that have changed (without the data itself). This mode is less system intensive, but is important for later when we wish to update the `/backups/sda1-bkp` to reflect a later snapshot of the filesystem.

5) Continue using your system.
After the initial backup, the driver will probably be left in incremental mode the vast majority of time.


6) Move the incremental back to snapshot mode:

	dbdctl transition-to-snapshot /.datto1 0


This command requires the name of a new COW file to begin tracking changes again (here we chose `/.datto1`). At this point the driver is finished with our `/.datto` file we created in step 2. The `/.datto` file now contains a list of the blocks that have changed since our initial snapshot. We will use this in the next step to update our backed up image. It is important to not use the same file name that we specified in step 2 for this command. Otherwise, we would overwrite our list of changed blocks.

7) Copy the changes:

	img-merge /dev/datto0 /.datto /backups/sda1-bkp


Here we can use the img-merge tool included with the driver. It takes 3 parameters: a snapshot (`/dev/datto0`), the list of changed blocks (`/.datto` from step 1), and an original image (`/backups/sda1-bkp` created in step 3). It copies the blocks listed in the block list from the new snapshot to the existing image, effectively updating the image.

8) Clean up the leftover file:

	rm /.datto


9) Go back to step 4 and repeat:
Keep in mind it is important to specify a different COW file path for each use. If you use the same file name you will overwrite the list of changed blocks. As a result you will have to use dd to perform a full copy again instead of using the faster `img-merge` tool (which only copies the changed blocks).

If you wish to keep multiple versions of the image, we recommend that you copy your images a snapshotting filesystem (such as BTRFS or ZFS). You can then snapshot the images after updating them (step 3 for the full backup or 7 the differential). This will allow you to keep a history of revisions to the image.

