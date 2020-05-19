# elastio-snap Inner Workings

## Introduction

The elastio-snap module manages snapshots and tracking changes by implementing a [copy-on-write (COW)](http://en.wikipedia.org/wiki/Copy-on-write) system. When first initialized for a block device, the kernel module creates a COW datastore file on the drive. The kernel module intercepts all writes at the block level, and copies the data that is about to be changed into the snapshot store before letting the write complete. In this way, the module can maintain a consistent and complete snapshot of the filesystem even if writes are occurring. The snapshot data is managed and accessed by the kernel module using an index located at the beginning of the on-disk COW file. This implementation allows our copy-on-write system to reliably maintain complete point-in-time images of a given drive at the block level, which means that it is more reliable and consistent than other methods of achieving our goal. 


### Vocabulary
* Sector: A fixed-size block of user-accessible data on a block device. It is defined by the kernel as 512 bytes.
* Block: A chunk of contiguous sectors. In our case, 4096 bytes, or 8 sectors.
* Section: The basic unit of size that the COW manager works with. 4096 _sectors_ long.

## COW File Overview
The COW snapshot file can be in one of three states:
* Dormant
* Active
* Unverified

and one of two modes:
* Snapshot
* Incremental

A snapshot device is in exactly one mode and exactly one state at any given time. For example, a snapshot device can be in incremental mode and active state, or in snapshot mode and dormant state. However, a snapshot device cannot be in active state and unverified state at the same time, nor can it be in snapshot mode and incremental mode at the same time.

### Snapshot Device States
Snapshot devices are transitioned between states depending on the state of the block device they are tracking. A snapshot device will spend most of its time in the "active" state. In this state, the snapshot device is fully initialized and all in-memory data structures are active. A snapshot will stay in active mode as long as the block device is mounted. 

When the block device is unmounted, whether by user action or during normal shutdown of the computer, the block device will transition to "dormant" state. In a nutshell, "dormant" state is a state in which the block device can be safely unmounted by the operating system. When an unmount system call is received for a block device that the elastio-snap module is tracking, the kernel module first writes all its in-memory data structures for that snapshot device to the COW file for later use, closes all open file descriptors to that file, and shuts down all processing threads for that block device. At this point, the volume can be unmounted by the operating system. The snapshot will stay in dormant mode until the disk is again re-mounted, at which point it will transition back to active state. This state is important because it allows snapshots and tracking to persist across unmounts. 

The last state is the "unverified" state. This state allows our snapshot devices and tracking to remain consistent and persistent across reboots. During the early boot process, the kernel module can be initialized with all of the drives' data structures before the tracked drives themselves are mounted by the OS. This allows the kernel module to immediately start tracking drives as they are mounted during system startup. This ensures that the module captures each and every single write to tracked block devices, which allows us to maintain 100% consistent and accurate change data for each volume. Once the drives themselves are mounted by the operating system, the kernel module transitions them to the active state.

### Snapshot Device Modes
Snapshot devices can be in either incremental mode or snapshot mode. 

In snapshot mode, the on-disk COW file maintains an on-disk block cache. This cache takes up roughly 10% of the total size of the drive, and is used to implement the copy-on-write mechanism during the backup process. While the snapshot file is in snapshot mode, whenever a write is made, all blocks set to be updated are first copied into the on-disk cache before the real write goes through. This allows the kernel module to seamlessly present a consistent and accurate view of the filesystem at the point in time that the snapshot was created. 

In incremental mode, by contrast, only the COW index is kept on disk.  This frees up disk space for the rest of the filesystem to use. Writes are tracked in-memory and periodically synced to the index file as needed. During this mode, the driver does not present a readable snapshot device and simply tracks which blocks have changed in the COW index. As a result, there is no need for the more expensive on-disk block cache used during snapshot mode, as the kernel module does not need the original data from the filesystem. The index can later be used by a tool such as `update-img` (see `utils/` directory) to copy only the changed blocks between snapshots. The driver should spend the majority of its time in incremental mode, between backups.

## COW File Components
The COW datastore file is comprised of three main parts:
* Header
* Index
* COW datastore

The header and index are always present as part of the file. The datastore is allocated when the module transitions the disk to snapshot mode, and de-allocated when the disk goes back to incremental mode. 
###  COW File Header Layout

The COW file header comprises the first 4096 bytes of the file, split up as follows:

* 0x0000 - 0x0003 : File signature
* 0x0004 - 0x0007 : Flags
* 0x0008 - 0x000F : Location of file pointer
* 0x0010 - 0x0017 : Amount of disk space allocated for snapshot mode COW file
* 0x0018 - 0x001F : Sequence ID of snapshot
* 0x0020 - 0x002F : UUID for snapshot series
* 0x0030 - 0x0037 : Version of the header format
* 0x0038 - 0x003F : Number of changed blocks since snapshot
* 0x0040 - 0xFFFF : Empty

Most of the header is left empty, for future usage and alignment concerns.

### Index

The index is a record of what sections of the block device are currently changed from the last snapshot. This record is kept updated while the device is in incremental mode. 

### COW datastore
When in snapshot mode, this portion of the COW file exists to hold the old versions of sections as they are updated by the filesystem as the snapshot is being taken. By storing these old versions in the COW datastore, the kernel module can present a consistent view of the filesytem at the point in time the snapshot was initiated. By default, this temporary datastore is allocated 10% of the total space on the volume. Hopefully, snapshots should not take long enough that this space is exhausted before the snapshotting process is complete. When in incremental mode, this portion of the COW file is de-allocated and given back to the filesystem.

## In-Memory Layout

In memory, the index is an array of `cow_section` structs. A `cow_section` struct looks like this:
```c
struct cow_section{
    char has_data; //zero if this section has mappings (on file or in memory)
    unsigned long usage; //counter that keeps track of how often this section is used
    uint64_t *mappings; //array of block addresses
};
```

Each struct corresponds to one section on-disk. When a block device is first initialized as a tracked device, `has_data` and `usage` are both to set to 0, and `mappings` is initialized to null. When a section becomes in-use, `has_data` is set to 1, `usage` is incremented, and `mappings` points to the data itself, which is stored in memory until it needs to be flushed to disk. For the purpose of storing mappings data, the module allocates an amount of memory per tracked block device. When a block device's in-memory mappings buffer fills, the buffer is flushed to disk and the cow_section structs are updated accordingly. An important note is that every time the mappings buffer is flushed, all `usage` fields are set to 0. 

### Reloading Data During Boot

When the system is coming up during a reboot or power-on event, the elastio-snap module takes control of the block devices during early boot, in the initramfs stage. The reload process is much the same as the initialization process, with the key difference that the metadata is reloaded from the header instead of being generated. In addition, every section's `has_data` flag is set for all sections, indicating that the driver should assume all sections have data synced to disk without holes. This requires that elastio-snap is loaded into the kernel in the initramfs, before the volumes are mounted.

### Flushing Data to Disk

When the in-memory cache fills up, the driver uses the `usage` field of each `cow_section` struct to calculate which sections have been used the least. Then, operating on the assumption that the least-used sections are not necessarily needed in memory, the driver flushes the lesser-used half of sections to disk. After this operation, the cache is half-full, and contains only the more-used sections. The `usage` fields are then all reset. Note that even if a `cow_section`'s buffer has been flushed to disk, its `has_data` remains set. 
