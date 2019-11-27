## NAME

update-img - Update a backup image with assurio-snap COW file.

## SYNOPSIS

`update-img <snapshot device> <cow file> <image file>`

## DESCRIPTION

`update-img` is a simple tool to efficiently update backup images made by the assurio-snap kernel module. It uses the leftover COW file from assurio-snap's incremental state to efficiently update an existing backup image. See the man page on `aioctl` for an example use case.

### EXAMPLES

`# update-img /dev/assurio-snap4 /var/backup/assurio1 /mnt/data/backup-img`

This command will update a previously backed up snapshot `/mnt/data/backup-img` with the changed blocks indicated by `/var/backup/assurio1` from `/dev/assurio-snap4`.

NOTE: `<snapshot device>` MUST be the NEXT snapshot after the one that `<image file>` was copied from.

## Bugs

## Author

    Tom Caputi (tcaputi@datto.com)
