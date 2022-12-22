#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only

#
# Copyright (C) 2019 Datto, Inc.
# Additional contributions by Elastio Software, Inc are Copyright (C) 2020 Elastio Software Inc.
#

import errno
import os
import subprocess
import unittest
from random import randint

import elastio_snap
import util
from devicetestcase import DeviceTestCase

class TestSetup(DeviceTestCase):
    def setUp(self):
        self.cow_file = "cow.snap"
        self.cow_full_path = "{}/{}".format(self.mount, self.cow_file)
        self.snap_device = "/dev/elastio-snap{}".format(self.minor)

    def test_setup_invalid_minor(self):
        self.assertEqual(elastio_snap.setup(1000, self.device, self.cow_full_path), errno.EINVAL)
        self.assertFalse(os.path.exists(self.snap_device))
        self.assertIsNone(elastio_snap.info(self.minor))

    def test_setup_volume_path_is_dir(self):
        self.assertEqual(elastio_snap.setup(self.minor, self.mount, self.cow_full_path), errno.ENOTBLK)
        self.assertFalse(os.path.exists(self.snap_device))
        self.assertIsNone(elastio_snap.info(self.minor))

    def test_setup_cow_file_path_is_dir(self):
        self.assertEqual(elastio_snap.setup(self.minor, self.device, self.mount), errno.EISDIR)
        self.assertFalse(os.path.exists(self.snap_device))
        self.assertIsNone(elastio_snap.info(self.minor))

    def test_setup_unmounted_volume(self):
        util.unmount(self.mount)
        self.addCleanup(util.mount, self.device, self.mount)

        self.assertEqual(elastio_snap.setup(self.minor, self.device, self.cow_full_path), errno.EINVAL)
        self.assertFalse(os.path.exists(self.snap_device))
        self.assertIsNone(elastio_snap.info(self.minor))

    def test_setup_readonly_volume(self):
        util.mount(self.device, self.mount, opts="remount,ro")
        self.addCleanup(util.mount, self.device, self.mount, opts="remount,rw")

        self.assertEqual(elastio_snap.setup(self.minor, self.device, self.cow_full_path), errno.EINVAL)
        self.assertFalse(os.path.exists(self.snap_device))
        self.assertIsNone(elastio_snap.info(self.minor))

    def test_setup_already_tracked_volume(self):
        self.assertEqual(elastio_snap.setup(self.minor, self.device, self.cow_full_path), 0)
        self.addCleanup(elastio_snap.destroy, self.minor)

        self.assertEqual(elastio_snap.setup(self.minor, self.device, self.cow_full_path), errno.EBUSY)
        self.assertTrue(os.path.exists(self.snap_device))
        self.assertIsNotNone(elastio_snap.info(self.minor))

    def test_setup_volume(self):
        self.assertEqual(elastio_snap.setup(self.minor, self.device, self.cow_full_path), 0)
        self.addCleanup(elastio_snap.destroy, self.minor)

        self.assertTrue(os.path.exists(self.snap_device))

        snapdev = elastio_snap.info(self.minor)
        self.assertIsNotNone(snapdev)

        self.assertEqual(snapdev["error"], 0)
        self.assertEqual(snapdev["state"], elastio_snap.State.ACTIVE | elastio_snap.State.SNAPSHOT)
        self.assertEqual(snapdev["cow"], "/{}".format(self.cow_file))
        self.assertEqual(snapdev["bdev"], self.device)
        self.assertEqual(snapdev["version"], 1)
        self.assertEqual(snapdev["ignore_snap_errors"], False)

    def test_setup_2_volumes(self):
        # Setup device #1 at the root volume
        minor = randint(0, 23)
        while minor == self.minor:
          minor = randint(0, 23)

        cmd = ["findmnt", "/", "-n", "-o", "SOURCE"]
        device = subprocess.check_output(cmd, timeout=10, shell=False).rstrip().decode("utf-8")
        # Convert LVM logical volume name (if it is) to the kernel bdev name
        cmd = ["readlink", "-f", device]
        device = subprocess.check_output(cmd, timeout=10).rstrip().decode("utf-8")
        snap_device = "/dev/elastio-snap{}".format(minor)
        cow_file = "cow"

        self.assertEqual(elastio_snap.setup(minor, device, "/{}".format(cow_file)), 0)

        # Check the snapshot device exists and alive
        self.assertTrue(os.path.exists(snap_device))

        snapdev = elastio_snap.info(minor)
        self.assertIsNotNone(snapdev)

        self.assertEqual(snapdev["error"], 0)
        self.assertEqual(snapdev["state"], elastio_snap.State.ACTIVE | elastio_snap.State.SNAPSHOT)
        self.assertEqual(snapdev["cow"], "/{}".format(cow_file))
        self.assertEqual(snapdev["bdev"], device)
        self.assertEqual(snapdev["version"], 1)
        self.assertEqual(snapdev["ignore_snap_errors"], False)

        # Setup device number 2, as ususally on an external disk or on a loopback device
        self.assertEqual(elastio_snap.setup(self.minor, self.device, self.cow_full_path, ignore_snap_errors=True), 0)
        self.addCleanup(elastio_snap.destroy, self.minor)

        # Check the 2nd snapshot device
        self.assertTrue(os.path.exists(self.snap_device))

        snapdev = elastio_snap.info(self.minor)
        self.assertIsNotNone(snapdev)

        self.assertEqual(snapdev["error"], 0)
        self.assertEqual(snapdev["state"], elastio_snap.State.ACTIVE | elastio_snap.State.SNAPSHOT)
        self.assertEqual(snapdev["cow"], "/{}".format(self.cow_file))
        self.assertEqual(snapdev["bdev"], self.device)
        self.assertEqual(snapdev["version"], 1)
        self.assertEqual(snapdev["ignore_snap_errors"], True)

        # Destroy 1st snapshot device
        self.assertEqual(elastio_snap.destroy(minor), 0)

if __name__ == "__main__":
    unittest.main()
