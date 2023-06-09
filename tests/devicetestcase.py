# SPDX-License-Identifier: GPL-2.0-only

#
# Copyright (C) 2019 Datto, Inc.
# Additional contributions by Elastio Software, Inc are Copyright (C) 2020 Elastio Software Inc.
#

import os
import subprocess
import unittest

import kmod
import util

from random import randint

@unittest.skipUnless(os.geteuid() == 0, "Must be run as root")
class DeviceTestCase(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.minor = randint(0, 23)
        seeds = []
        cls.backing_stores = []
        cls.devices = []
        cls.size_mb = 512
        cls.is_raid = False

        cls.kmod = kmod.Module("../src/elastio-snap.ko")
        cls.kmod.load(debug=1)
        if os.getenv('TEST_DEVICES'):
            cls.devices = os.getenv('TEST_DEVICES').split()
            for device in cls.devices:
                cls.size_mb = util.dev_size_mb(device)
                util.dd("/dev/zero", device, cls.size_mb, bs="1M")
        else:
            dev_count = 2 if os.getenv('LVM') or os.getenv('RAID') else 1
            for i in range(dev_count):
                # Unexpectedly randint can generate 2 same numbers in a row.
                # As result, we'll have 2 loop devices with the same one file as backing store.
                # And then mdadm will fail to create a mirror from these "2 different" loop devices, because 2nd will always race 'device busy' error.
                # So, let's verify the difference of these random numbers )
                while True:
                    r = randint(0, 999)
                    if not r in seeds: break
                seeds.append(r)
                cls.backing_stores.append("/tmp/disk_{0:03d}.img".format(seeds[i]))
                util.dd("/dev/zero", cls.backing_stores[i], cls.size_mb, bs="1M")
                cls.devices.append(util.loop_create(cls.backing_stores[i]))

        if len(cls.devices) == 1:
            cls.device = cls.devices[0]
        elif os.getenv('LVM'):
            cls.device = util.assemble_mirror_lvm(cls.devices, cls.minor)
        elif os.getenv('RAID'):
            cls.is_raid = True
            cls.device = util.assemble_mirror_raid(cls.devices, cls.minor)

        cls.fs = os.getenv('TEST_FS', 'ext4')
        util.mkfs(cls.device, cls.fs)
        cls.mount = "/tmp/elastio-snap_{0:03d}".format(cls.minor)
        os.makedirs(cls.mount, exist_ok=True)
        util.mount(cls.device, cls.mount)

    @classmethod
    def tearDownClass(cls):
        util.unmount(cls.mount)

        if os.getenv('LVM'):
            util.disassemble_mirror_lvm(cls.device)

        if os.getenv('RAID'):
            util.disassemble_mirror_raid(cls.device, cls.devices)

        # Destroy loopback devices and unlink their storage
        if not os.getenv('TEST_DEVICES'):
            for device in cls.devices:
                util.loop_destroy(device)
            for backing_store in cls.backing_stores:
                os.unlink(backing_store)

        os.rmdir(cls.mount)
        cls.kmod.unload()

        assert(util.kernel_warning_exists() == False)
