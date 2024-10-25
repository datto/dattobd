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

`dbdctl setup-snapshot [-c <cache size>] [-f <fallocate>] <block device> <cow file path> <minor>`

Sets up a snapshot of `<block device>`, saving all COW data to `<cow file path>`. The snapshot device will be `/dev/datto<minor>`. The minor number will be used as a reference number for all other `dbdctl` commands. `<cow file path>` must be a path on the `<block device>`.

### reload-snapshot

`dbdctl reload-snapshot [-c <cache size>] <block device> <cow file> <minor>`

Reloads a snapshot. This command is meant to be run before the block device is mounted, after a reboot or after the driver is unloaded. It notifies the kernel driver to expect the block device specified to come back online. This command requires that the snapshot was cleanly unmounted in snapshot mode beforehand. If this is not the case, the snapshot will be put into the failure state once it attempts to come online. The minor number will be used as a reference number for all other `dbdctl` commands.

### reload-incremental

`dbdctl reload-incremental [-c <cache size>] <block device> <cow file> <minor>`

Reloads a block device that was in incremental mode. See `reload-snapshot` for restrictions.

### transition-to-incremental

`dbdctl transition-to-incremental <minor>`

Transitions a snapshot COW file to incremental mode, which only tracks which blocks have changed since a snapshot started. This will remove the associated snapshot device.

### transition-to-snapshot

`dbdctl transition-to-snapshot [-f <fallocate>] <cow file> <minor>`

Transitions a block device in incremental mode to snapshot mode. This call ensures no writes are missed between tearing down the incremental and setting up the new snapshot. The new snapshot data will be recorded in `<cow file>`. The old cow file will still exist after this and can be used to efficiently copy only changed blocks using a tool succh as `update-img`.

### destroy

`dbdctl destroy <minor>`

Cleanly and completely removes the snapshot or incremental, unlinking the associated COW file.

### reconfigure

`dbdctl reconfigure [-c <cache size>] <minor>`

Allows you to reconfigure various parameters of a snapshot while it is online. Currently only the index cache size (given in MB) can be changed dynamically.

### expand-cow-file

`dbdctl expand-cow-file <minor> <size>`

Expands cow file in snapshot mode by size (given in bytes).

### EXAMPLES

`# dbdctl setup-snapshot /dev/sda1 /var/backup/datto 4`

This command will set up a new COW snapshot device tracking `/dev/sda1` at `/dev/datto4`. This block device is backed by a new file created at the path `/var/backup/datto`.

`# dbdctl transition-to-incremental 4`

Transitions the snapshot specified by the minor number to incremental mode.

`# dbdctl transition-to-snapshot /var/backup/datto1 4`

Cleanly transitions the incremental to a new snapshot, using `/var/backup/datto1` as the new COW file. At this point a second backup can be taken, either doing a full copy with a tool like `dd` or an incremental copy using a tool such as `update-img`, if a previous snapshot backup exists.

`# dbdctl reconfigure -c 400 4`

Reconfigures the block device to have an in-memory index cache size of 400 MB.

`# dbdctl destroy 4`

This will stop tracking `/dev/sda1`, remove the associated `/dev/datto4` (since the device is in snapshot mode), delete the COW file backing it, and perform all other cleanup.

`# dbdctl reload-snapshot /dev/sda1 /var/backup/datto1 4`

After a reboot, this command may be performed in the early stages of boot, before the block device is mounted read-write. This will notify the driver to expect a block device `/dev/sda1` that was left in snapshot mode to come online with a COW file located at `/var/backup/datto1` (relative to the mountpoint), and that the reloaded snapshot should come online at minor number 4. If a problem is discovered when the block device comes online, this block device will be put into the failure state, which will be reported in `/proc/datto-info`

`# dbdctl reload-incremental /dev/sda5 /var/backup/datto1 4`

This will act the same as `reload-snapshot`, but for a device that was left in incremental mode.

## Bugs

## Author

    Tom Caputi (tcaputi@datto.com)
