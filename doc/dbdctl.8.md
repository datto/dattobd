## NAME

dattobd - Control the Datto block device kernel module.

## SYNOPSIS

`dbdctl <sub-command> [<args>]`

## DESCRIPTION

`dbdctl` is the userspace tool used to manage the dattobd kernel module. It provides an interface to create, delete, reload, transition, and configure on-disk snapshots and certain parameters of the kernel module itself.

This manual page describes `dbdctl` briefly. More detail is available in the Git repository located at https://github.com/datto/dattobd. 

## OPTIONS
    -c cache-size
         Specify how big the in-memory data cache can grow to (in MB). Defaults to 300 MB.

    -f fallocate
         Specify the maximum size of the COW file on disk.

## SUB-COMMANDS

### setup-snapshot

`dbdctl setup-snapshot [-c <cache size>] [-f <fallocate>] <block device> <cow file path> <minor number>`

Sets up a snapshot.

### reload-snapshot

`dbdctl reload-snapshot [-c <cache size>] <block device> <cow file> <minor>`

Reloads a COW file that was in snapshot mode.

This command is meant to be run before block devices are mounted. It notifies the kernel driver to expect the block device specified to come back online.

### reload-incremental

`dbdctl reload-incremental [-c <cache size>] <block device> <cow file> <minor>`

Reloads a COW file that was in incremental mode.

### transition-to-snapshot

`dbdctl transition-to-snapshot [-f <fallocate>] <cow file> <minor>`

Transitions a COW file in incremental mode to snapshot mode.

### transition-to-incremental

`dbdctl transition-to-incremental <minor>`

Transitions a snapshot COW file to incremental mode.

### destroy

`dbdctl destroy <minor>`

Cleanly and completely removes the snapshot or incremental file corresponding to the given block device minor number.

### reconfigure

`dbdctl reconfigure [-c <cache size>] <minor>`

Allows you to reconfigure various parameters of a snapshot while it is online.

### EXAMPLES

`# dbdctl setup-snapshot /dev/sda1 /var/backup/datto-backup 4`

This command will setup a new COW snapshot device tracking `/dev/sda1` at `/dev/datto4`. This block device is backed by a new file created at the path `/var/backup/datto-backup`.

NOTE: The COW file responsible for maintaining snapshots of a filesystem MUST RESIDE on the SAME block device as the filesystem it is snapshotting!

`# dbdctl reload-snapshot /dev/sda1 /var/backup/datto-backup 4`

This will notify the driver that the COW snapshot file located at `/var/backup/datto-backup` is tracking `/dev/sda1`, and that the resulting block device should be `/dev/datto4`.

NOTE: The path to the COW file is relative _TO THE ROOT OF THE VOLUME ON WHICH THE FILE IS LOCATED_, **NOT** `/`!

`# dbdctl reload-incremental /dev/sda5 /var/backup/datto-backup <whatever the minor number is>`

This will notify the driver that the COW incremental file located at `/var/backup/datto-backup` is tracking `/dev/sda1`, and that the resulting block device created by the driver should be `/dev/datto4`.

NOTE: The path to the COW file is relative _TO THE ROOT OF THE VOLUME ON WHICH THE FILE IS LOCATED_, **NOT** `/`!

`# dbdctl destroy 5`

This will destroy the block device located at `/dev/datto5`, delete the COW file backing it, and perform all other cleanup relating to that device.

`# dbdctl transition-to-incremental 5`

Transitions the Datto device specified by the minor number to incremental mode.

`# dbdctl transition-to-snapshot /var/backup/datto-backup 5`

Transitions the incremental COW file indicated by the minor number to snapshot mode.

`# dbdctl reconfigure -c 400 5`

Reconfigures the snapshot at `/dev/datto5` to have an in-memory cache size of 400 MB.

## Bugs

## Author

    Tom Caputi (tcaputi@datto.com)

## See Also

`dlad`(1)
