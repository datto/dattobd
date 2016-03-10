## NAME

dattobd - Control the Datto block device kernel module.

## SYNOPSIS

`update-img <snapshot device> <cow file> <image file>`

## DESCRIPTION

`update-img` is a simple tool to efficiently update backup images made by the dattobd kernel module. It uses the leftover COW file from dattobd's incremental state to efficiently update an existing backup image. See the man page on `dbdctl` for an example use case.

### EXAMPLES

`# update-img /dev/datto4 /var/backup/datto1 /mnt/data/backup-img`

This command will update a previously backed up snapshot `/mnt/data/backup-img` with the changed blocks indicated by `/var/backup/datto1` from `/dev/datto4`.

NOTE: `<snapshot device>` MUST be the NEXT snapshot after the one that `<image file>` was copied from. This tool is currently very simple and does not check that this is the case. If this is not true, the updated image will not include any of the writes performed during the intermediate snapshot and the image will be corrupted.

## Bugs

## Author

    Tom Caputi (tcaputi@datto.com)
