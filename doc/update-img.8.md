## NAME

update-img - Update a backup image with elastio-snap COW file.

## SYNOPSIS

`update-img <snapshot device> <cow file> <image file>`

## DESCRIPTION

`update-img` is a simple tool to efficiently update backup images made by the elastio-snap kernel module. It uses the leftover COW file from elastio-snap's incremental state to efficiently update an existing backup image. See the man page on `elioctl` for an example use case.

### EXAMPLES

`# update-img /dev/elastio-snap4 /var/backup/elastio1 /mnt/data/backup-img`

This command will update a previously backed up snapshot `/mnt/data/backup-img` with the changed blocks indicated by `/var/backup/elastio1` from `/dev/elastio-snap4`.

NOTE: `<snapshot device>` MUST be the NEXT snapshot after the one that `<image file>` was copied from.

## Bugs

## Author

    Tom Caputi (tcaputi@datto.com)
