# SPDX-License-Identifier: GPL-2.0-only

#
# Copyright (C) 2017 Datto, Inc.
#

# Initialize environment variables
function init_env
{
    DBDCTL="${TEST_DIR}/../app/dbdctl"
    UPDATE_IMG="${TEST_DIR}/../utils/update-img"
    KMOD="dattobd.ko"
    KMOD_PATH="${TEST_DIR}/../src/${KMOD}"
    DEV_DIR="/var/tmp/dattobd/dev"
    MOUNT_DIR="/var/tmp/dattobd/mnt"
    DISK="${DEV_DIR}/disk0"
    DEVICE="/dev/loop4"
    MOUNT="${MOUNT_DIR}/dev0"
    COW_FILE="cow.snap"
    MINOR=1

    export LD_LIBRARY_PATH=../lib
}

function insert_module
{
    insmod ${KMOD_PATH} $@ && return 0

    echo "Unable to load module" >&2
    return 1
}

function remove_module
{
    rmmod ${KMOD} && return 0

    echo "Unable to remove module" >&2
    return 1
}

function is_mounted
{
    grep -qw "$1" /proc/mounts
}

function is_tracked
{
    grep -q "\"minor\": $1," /proc/datto-info
}

function md5
{
    [[ -e "$1" ]] || return 1
    md5sum "$1" | awk '{ print $1 }' || return 1
    return 0
}

# parse <minor> <field>
# Prints the value of the field for a given minor if it exists.
# Returns 0 on success, 1 otherwise.
#
function parse
{
    [[ "$#" == "2" ]] || return 1
    [[ -f "/proc/datto-info" ]] || return 1

    python2.7 ${TEST_DIR}/parse.py "$1" "$2"
}

# create_device <file> <size>
# Create a file of a given size in MiB.
# Returns 0 on success and 1 on error.
#
function create_device
{
    [[ "$#" == "2" ]] || return 1

    local file="$1"
    local size="$2"

    local dir=$(dirname $file)
    mkdir -p "${dir}" || return 1

    if command -v fallocate &> /dev/null; then
        fallocate -l ${size}M "${file}" || return 1
    elif command -v truncate &> /dev/null; then
        truncate --size=${size}M "${file}" || return 1
    else
        dd if=/dev/zero of="${file}" bs=1M count=${size} || return 1
    fi

    sync
    sleep 0.1
    return 0
}

# format_device <device> <fs>
# Format a device with the given file system type.
# Returns 0 on success and 1 on error.
#
function format_device
{
    [[ "$#" == "2" ]] || return 1

    local dev="$1"
    local fs="$2"

    case "${fs}" in
        ext[2-4])
            mkfs.${fs} -F "${dev}" || return 1
            ;;
        fat|fat32|FAT|FAT32)
            mkfs.vfat -F 32 "${dev}" || return 1
            ;;
        ntfs|NTFS)
            mkfs.ntfs -F -f "${dev}" || return 1
            ;;
        xfs|XFS)
            mkfs.xfs -f "${dev}" || return 1
            ;;
        *)
            echo "Invalid file system type ${fs} given" >&2
            return 1
            ;;
    esac

    sync
    sleep 0.1
    return 0
}

# setup_device <file> <size> <fs> <loop> <mount>
# Create, format, and mount a loop device.
# Returns 0 on success and 1 on error.
#
function setup_device
{
    [[ "$#" == "5" ]] || return 1

    local file="$1"
    local size="$2"
    local fs="$3"
    local loop="$4"
    local mount="$5"

    create_device "${file}" ${size} || return 1
    format_device "${file}" ${fs} || return 1

    losetup ${loop} "${file}" || return 1
    sleep 0.1

    mkdir -p ${mount} || return 1
    mount ${loop} ${mount} || return 1
    sleep 0.1

    return 0
}

function setup_default_device
{
    if command -v mkfs.ext4 &> /dev/null; then
        setup_device ${DISK} 512 ext4 ${DEVICE} ${MOUNT}
    else
        setup_device ${DISK} 512 ext3 ${DEVICE} ${MOUNT}
    fi
}

# cleanup_device <file> <loop> <mount>
# Unmount and destroy a loop device and its backing file.
# Returns 0 on success and 1 on error.
#
function cleanup_device
{
    [[ "$#" == "3" ]] || return 1

    local file="$1"
    local loop="$2"
    local mount="$3"

    if is_mounted ${loop}; then
        umount ${mount} || return 1
    fi

    sleep 0.1

    if losetup ${loop} &> /dev/null; then
        losetup -d ${loop} || return 1
    fi

    sleep 0.1

    rm -f ${file} || return 1
    sync

    sleep 0.1
    return 0
}

function cleanup_default_device
{
    cleanup_device ${DISK} ${DEVICE} ${MOUNT}
}
