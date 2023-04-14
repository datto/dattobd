# SPDX-License-Identifier: GPL-2.0-only

#
# Copyright (C) 2022 Elastio Software Inc.
#

import os
import subprocess
import unittest

import kmod
import util

from random import randint

@unittest.skipUnless(os.geteuid() == 0, "Must be run as root")
@unittest.skipIf(os.getenv('LVM') or os.getenv('RAID'), "Multipart testcase does not support LVM/raid devices creation")
@unittest.skipIf(os.getenv('TEST_DEVICES') and util.get_disk_by_partition(os.getenv('TEST_DEVICES').split()[0]), "Multipart testcase requires disk, not a partition.")
class DeviceTestCaseMultipart(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # For now let's hardcode 7 partitions
        cls.part_count = 7
        cls.minors = []
        for i in range(cls.part_count):
            # Unexpectedly randint can generate 2 same numbers in a row.
            # So, let's verify the difference of the random numbers.
            while True:
                r = randint(0, 23)
                if not r in cls.minors: break
            cls.minors.append(r)

        cls.kmod = kmod.Module("../src/elastio-snap.ko")
        cls.kmod.load(debug=1)

        if os.getenv('TEST_DEVICES'):
            # We need just 1st device to create partitions on it
            cls.device = os.getenv('TEST_DEVICES').split()[0]
            util.wipefs(cls.device)
            util.dd("/dev/zero", cls.device, util.dev_size_mb(cls.device), bs="1M")
            util.partition(cls.device, cls.part_count)
        else:
            cls.backing_store = ("/tmp/disk_{0:03d}.img".format(cls.minors[0]))
            util.dd("/dev/zero", cls.backing_store, 256, bs="1M")
            cls.device = util.loop_create(cls.backing_store, cls.part_count)

        cls.devices = []
        cls.devices += util.get_partitions(cls.device)

        cls.mounts = []
        cls.fs = os.getenv('TEST_FS', 'ext4')
        for i in range(cls.part_count):
            util.mkfs(cls.devices[i], cls.fs)
            cls.mounts.append("/tmp/elio-dev-mnt_{0:03d}".format(cls.minors[i]))
            os.makedirs(cls.mounts[i], exist_ok=True)
            util.mount(cls.devices[i], cls.mounts[i])

    @classmethod
    def tearDownClass(cls):
        for mount in cls.mounts:
            util.unmount(mount)
            os.rmdir(mount)

        # Destroy loopback device and unlink its storage
        if not os.getenv('TEST_DEVICES'):
            util.loop_destroy(cls.device)
            os.unlink(cls.backing_store)

        cls.kmod.unload()
