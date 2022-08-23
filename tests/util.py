#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only

#
# Copyright (C) 2019 Datto, Inc.
# Additional contributions by Elastio Software, Inc are Copyright (C) 2020 Elastio Software Inc.
#

import hashlib
import subprocess
import sys
import time


def mount(device, path, opts=None):
    cmd = ["mount", device, path]
    if opts:
        cmd += ["-o", opts]

    subprocess.check_call(cmd, timeout=10)


def unmount(path, retry_on_dev_busy=True):
    cmd = ["umount", path]
    # subprocess.run is introduced in Python 3.5
    if not retry_on_dev_busy or sys.version_info <= (3, 5):
        subprocess.check_call(cmd, timeout=10)
    else:
        # The retries on device busy error are necessary on Ubuntu 22.04, kernel 5.15
        # for the tests test_destroy_unverified_incremental and test_destroy_unverified_snapshot.
        # See https://github.com/elastio/elastio-snap/issues/138
        retries = 3
        for retry in range(retries):
            p = subprocess.run(cmd, timeout=20, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            if p.returncode == 0:
                break
            elif retry + 1 < retries and "busy" not in p.stderr.decode():
                print("Command umount " + path + " has failed (" + str(p.returncode) + "): " + p.stderr.decode())
                raise subprocess.CalledProcessError(p.returncode, cmd, "Command failed")
            elif retry + 1 == retries:
                raise subprocess.CalledProcessError(p.returncode, cmd, "Command failed " + str(retries) + "times")
            else:
                time.sleep(1)


def dd(ifile, ofile, count, **kwargs):
    cmd = ["dd", "status=none", "if={}".format(ifile), "of={}".format(ofile), "count={}".format(count)]
    for k, v in kwargs.items():
        cmd.append("{}={}".format(k, v))

    subprocess.check_call(cmd, timeout=30)


def md5sum(path):
    md5 = hashlib.md5()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(4096), b""):
            md5.update(chunk)

    return md5.hexdigest()


def settle(timeout=20):
    cmd = ["udevadm", "settle", "-t", "{}".format(timeout)]
    subprocess.check_call(cmd, timeout=(timeout + 10))

def partprobe(device, timeout=30):
        cmd = ["partprobe", device]
        subprocess.check_call(cmd, timeout=timeout)

def udev_start_exec_queue():
    cmd = ["udevadm", "control", "--start-exec-queue"]
    subprocess.check_call(cmd)


def udev_stop_exec_queue():
    cmd = ["udevadm", "control", "--stop-exec-queue"]
    subprocess.check_call(cmd)


def partition(disk, part_count = 0):
    if part_count == 0:
        return disk

    part_type = "primary"
    part_size_percent = 100 // part_count
    cmd = ["parted", "--script", "--align", "optimal", disk, "mklabel", "gpt"]
    for start in range(0, 100, part_size_percent):
        end = start + part_size_percent
        if end > 100: break
        if end + part_size_percent > 100: end = 100
        cmd.append("mkpart " + part_type + " {}% {}%".format(start, end))

    subprocess.check_call(cmd, timeout=30)
    partprobe(disk)
    settle()

    return disk


def loop_create(path, part_count = 0):
    cmd = ["losetup", "--find", "--show", "--partscan", path]
    loopdev = subprocess.check_output(cmd, timeout=10).rstrip().decode("utf-8")

    if part_count > 0:
        partition(loopdev, part_count)

    return loopdev


def loop_destroy(loop):
    cmd = ["losetup", "-d", loop]
    subprocess.check_call(cmd, timeout=10)


def mkfs(device, fs="ext4"):
    if (fs == "xfs"):
        cmd = ["mkfs.xfs", device, "-f"]
    else:
        cmd = ["mkfs." + fs, "-F", device]

    subprocess.check_call(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=120)


def dev_size_mb(device):
    return int(subprocess.check_output("blockdev --getsize64 %s" % device, shell=True))//1024**2


# This method finds names of the partitions of the disk
def get_partitions(disk):
    # The output of this command 'lsblk /dev/loop0 -l -o NAME -n' is something like
    # loop0
    # loop0p1
    # but sometimes the order is random
    cmd = ["lsblk", disk, "-l", "-o", "NAME", "-n"]
    disk_and_parts = subprocess.check_output(cmd, timeout=10).rstrip().decode("utf-8").splitlines()
    disk_and_parts.sort()
    parts = ['/dev/{}'.format(part) for part in disk_and_parts]
    parts.remove(disk)
    return parts


# This method finds name of the last partition of the disk
def get_last_partition(disk):
    return get_partitions(disk)[-1]


def get_disk_by_partition(part):
    cmd = ["lsblk", "-ndo", "pkname", part]
    return subprocess.check_output(cmd, timeout=10).rstrip().decode("utf-8")


def wipefs(device):
    cmd = ["wipefs", "--all", "--force", "--quiet", device]
    subprocess.check_call(cmd, timeout=1220)


def parted_create_lvm_raid_partitions(devices, kind):
    if kind == "lvm":
        part_type="LVM2"
    elif kind == "raid":
        part_type="RAID"
    else:
        raise ValueError("Wrong argument kind '" + kind + "' is not 'lvm' or 'raid'!")

    settle()
    partitions=[]
    for device in devices:
        wipefs(device)
        cmd = ["parted", "--script", device, "mklabel gpt"]
        subprocess.check_call(cmd, timeout=30)
        cmd = ["parted", "--script", device, "mkpart '" + part_type + "' 0% 100%"]
        subprocess.check_call(cmd, timeout=30)
        cmd = ["parted", "--script", device, "set 1 " + kind + " on"]
        subprocess.check_call(cmd, timeout=30)
        partprobe(device)
        settle()
        part = get_last_partition(device)
        # mdadm rarely and randomly complains on create about superblock
        # let's clean it up and do not care about return code
        wipefs(part)
        partitions.append(part)

    return partitions


def mdadm_zero_superblock(partition):
    cmd = ["mdadm", "--zero-superblock", partition]
    # subprocess.run is introduced in Python 3.5
    if sys.version_info <= (3, 5):
        subprocess.check_call(cmd, timeout=10)
    else:
        # We don't care about the possible errors
        subprocess.run(cmd, timeout=10, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def assemble_mirror_lvm(devices, seed):
    # 1. Create LVM partitions
    partitions = parted_create_lvm_raid_partitions(devices, "lvm")

    # 2. Create physical volume.  The command looks like 'pvcreate /dev/sdb1 /dev/sdc1'
    cmd = ["pvcreate"]
    cmd += partitions
    subprocess.check_call(cmd, timeout=10)

    # 3. Create volume group.  The command looks like 'vgcreate volgroup_mirror /dev/sdb1 /dev/sdc1'
    vol_group = "vg_mirror" + str(seed)
    logical_vol = "lv_mirror" + str(seed)
    cmd = ["vgcreate", vol_group]
    cmd += partitions
    subprocess.check_call(cmd, timeout=10)

    # 4. Create logical volume with mirroring.  The command looks like 'lvcreate -L 230MB -m1 -n vg_mirror lv_mirror'
    dev_size = dev_size_mb(devices[1])
    log_vol_size = str(dev_size - int(dev_size/10)) + "MB"
    cmd = ["lvcreate", "-L", log_vol_size, "-m1", "-n", logical_vol, vol_group]
    subprocess.check_call(cmd, timeout=10)
    lvm_dev = "/dev/" + vol_group + "/" + logical_vol

    # 5. Convert LVM logical volume name to the kernel bdev name like /dev/vg_mirror22/lv_mirror22 to the /dev/dm-4 or so
    cmd = ["readlink", "-f", lvm_dev]
    return subprocess.check_output(cmd, timeout=10).rstrip().decode("utf-8")


def disassemble_mirror_lvm(lvm_device):
    # 0. Preperation. Convert /dev/dm-X kernel bdev name or any kind of the LVM device name to the /dev/mapper/vg_name-lv_name
    cmd = ["find", "-L", "/dev/mapper", "-samefile", lvm_device]
    lvm_device = subprocess.check_output(cmd, timeout=10).rstrip().decode("utf-8")

    # 1. Disable LVM.  The command looks like 'lvchange -an /dev/mapper/vg_mirror22-lv_mirror22'
    cmd = ["lvchange", "-an", lvm_device]
    subprocess.check_call(cmd, timeout=10)

    # 2. Delete LVM volume.  The command looks like 'lvremove /dev/mapper/vg_mirror22-lv_mirror22'
    cmd = ["lvremove", "-f", lvm_device]
    subprocess.check_call(cmd, timeout=10)

    # 3. Disable volume group.  The command looks like 'vgremove vg_mirror22'
    vol_group = lvm_device.replace("/dev/mapper/", "").split("-")[0]
    cmd = ["vgremove", vol_group]
    subprocess.check_call(cmd, timeout=10)


def assemble_mirror_raid(devices, seed):
    # 1. Create RAID partitions
    partitions = parted_create_lvm_raid_partitions(devices, "raid")

    # 2. Create RAID 1 array.
    #    udev control --stop-exec-queue and retries are workarounds for the mdadm's flacky issue, when it fails like this:
    #    'mdadm: ADD_NEW_DISK for /dev/loop1p1 failed: Device or resource busy'
    raid_dev = "/dev/md" + str(seed)
    cmd = ["mdadm", "--create", "--quiet", "--auto=yes", "--force", "--metadata=0.90", raid_dev, "--level=1", "--raid-devices=" + str(len(partitions))]
    cmd += partitions
    # subprocess.run is introduced in Python 3.5
    if sys.version_info <= (3, 5):
        subprocess.check_call(cmd, timeout=20)
    else:
        retries = 3
        for retry in range(retries):
            udev_stop_exec_queue()
            time.sleep(1)
            rc = subprocess.run(cmd, timeout=20).returncode
            udev_start_exec_queue()
            if rc == 0:
                break
            elif retry + 1 < retries:
                subprocess.run(["mdadm", "--stop", "--quiet", raid_dev], timeout=15, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                for part in partitions:
                    mdadm_zero_superblock(part)
            elif retry + 1 == retries:
                raise subprocess.CalledProcessError(rc, cmd, "Command failed " + str(retries) + "times")

    return raid_dev


def disassemble_mirror_raid(raid_device, devices):
    udev_stop_exec_queue()
    cmd = ["mdadm", "--stop", "--quiet", raid_device]
    subprocess.check_call(cmd, timeout=30)
    udev_start_exec_queue()
    time.sleep(1)
    for device in devices:
        mdadm_zero_superblock(get_last_partition(device))
