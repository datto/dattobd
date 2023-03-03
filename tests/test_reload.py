#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only

#
# Copyright (C) 2022 Elastio Software Inc.
#

import errno
import os
import unittest
import platform
import elastio_snap
import util
from devicetestcase import DeviceTestCase


class TestReload(DeviceTestCase):
    def setUp(self):
        self.cow_file = "cow.snap"
        self.cow_full_path = "{}/{}".format(self.mount, self.cow_file)
        self.cow_reload_path = "/{}".format(self.cow_file)
        self.snap_device = "/dev/elastio-snap{}".format(self.minor)

        util.test_track(self._testMethodName, started=True)

    def tearDown(self):
        util.test_track(self._testMethodName, started=False)

    def test_reload_snap_invalid_minor(self):
        self.assertEqual(elastio_snap.reload_snapshot(1000, self.device, self.cow_reload_path), errno.EINVAL)
        self.assertFalse(os.path.exists(self.snap_device))
        self.assertIsNone(elastio_snap.info(self.minor))

    def test_reload_inc_invalid_minor(self):
        self.assertEqual(elastio_snap.reload_incremental(1000, self.device, self.cow_reload_path), errno.EINVAL)
        self.assertFalse(os.path.exists(self.snap_device))
        self.assertIsNone(elastio_snap.info(self.minor))

    def test_reload_snap_volume_path_is_dir(self):
        self.assertEqual(elastio_snap.reload_snapshot(self.minor, self.mount, self.cow_full_path), errno.ENOTBLK)
        self.assertFalse(os.path.exists(self.snap_device))
        self.assertIsNone(elastio_snap.info(self.minor))

    def test_reload_inc_volume_path_is_dir(self):
        self.assertEqual(elastio_snap.reload_incremental(self.minor, self.mount, self.cow_full_path), errno.ENOTBLK)
        self.assertFalse(os.path.exists(self.snap_device))
        self.assertIsNone(elastio_snap.info(self.minor))

    def test_reload_snap_device_no_exists(self):
        self.assertEqual(elastio_snap.reload_snapshot(self.minor, "/dev/not_exist_device", self.cow_full_path), errno.ENOENT)
        self.assertFalse(os.path.exists(self.snap_device))
        self.assertIsNone(elastio_snap.info(self.minor))

    def test_reload_inc_device_no_exists(self):
        self.assertEqual(elastio_snap.reload_incremental(self.minor, "/dev/not_exist_device", self.cow_full_path), errno.ENOENT)
        self.assertFalse(os.path.exists(self.snap_device))
        self.assertIsNone(elastio_snap.info(self.minor))


    def test_reload_unverified_snapshot(self):
        util.unmount(self.mount)

        self.assertEqual(elastio_snap.reload_snapshot(self.minor, self.device, self.cow_reload_path, ignore_snap_errors=True), 0)
        self.addCleanup(elastio_snap.destroy, self.minor)
        self.addCleanup(util.mount, self.device, self.mount)
        self.assertFalse(os.path.exists(self.snap_device))

        self.check_snap_info(0, elastio_snap.State.UNVERIFIED | elastio_snap.State.SNAPSHOT, True)

        # Mount and test that the non-existent cow file been handled
        util.mount(self.device, self.mount)
        self.addCleanup(util.unmount, self.device, self.mount)

        self.check_snap_info(-errno.ENOENT, elastio_snap.State.UNVERIFIED | elastio_snap.State.SNAPSHOT, True)


    def test_reload_unverified_incremental(self):
        util.unmount(self.mount)

        self.assertEqual(elastio_snap.reload_incremental(self.minor, self.device, self.cow_reload_path, ignore_snap_errors=True), 0)
        self.addCleanup(elastio_snap.destroy, self.minor)
        self.addCleanup(util.mount, self.device, self.mount)
        self.assertFalse(os.path.exists(self.snap_device))

        self.check_snap_info(0, elastio_snap.State.UNVERIFIED, True)

        # Mount and test that the non-existent cow file been handled
        util.mount(self.device, self.mount)
        self.addCleanup(util.unmount, self.device, self.mount)

        self.check_snap_info(-errno.ENOENT, elastio_snap.State.UNVERIFIED, True)


    def test_reload_verified_snapshot(self):
        self.assertEqual(elastio_snap.setup(self.minor, self.device, self.cow_full_path, ignore_snap_errors=True), 0)

        util.unmount(self.mount)

        self.kmod.unload()
        self.kmod.load(debug=1)
        self.assertFalse(os.path.exists(self.snap_device))

        self.assertEqual(elastio_snap.reload_snapshot(self.minor, self.device, self.cow_reload_path), 0)
        self.addCleanup(elastio_snap.destroy, self.minor)
        self.addCleanup(util.mount, self.device, self.mount)
        self.assertFalse(os.path.exists(self.snap_device))

        self.check_snap_info(0, elastio_snap.State.UNVERIFIED | elastio_snap.State.SNAPSHOT, False)

        # Mount and test that snapshot is active
        util.mount(self.device, self.mount)
        self.addCleanup(util.unmount, self.device, self.mount)

        self.assertTrue(os.path.exists(self.cow_full_path))
        self.assertTrue(os.path.exists(self.snap_device))

        self.check_snap_info(0, elastio_snap.State.ACTIVE | elastio_snap.State.SNAPSHOT, False, 1)


    def test_reload_verified_inc(self):
        self.assertEqual(elastio_snap.setup(self.minor, self.device, self.cow_full_path, ignore_snap_errors=True), 0)
        self.assertEqual(elastio_snap.transition_to_incremental(self.minor), 0)

        util.unmount(self.mount)

        self.kmod.unload()
        self.kmod.load(debug=1)
        self.assertFalse(os.path.exists(self.snap_device))

        self.assertEqual(elastio_snap.reload_incremental(self.minor, self.device, self.cow_reload_path), 0)
        self.addCleanup(elastio_snap.destroy, self.minor)
        self.addCleanup(util.mount, self.device, self.mount)
        self.assertFalse(os.path.exists(self.snap_device))

        self.check_snap_info(0, elastio_snap.State.UNVERIFIED, False)

        # Mount and test that snapshot is active
        util.mount(self.device, self.mount)
        self.addCleanup(util.unmount, self.device, self.mount)

        self.assertTrue(os.path.exists(self.cow_full_path))
        self.assertFalse(os.path.exists(self.snap_device))

        self.check_snap_info(0, elastio_snap.State.ACTIVE, False, 1)


    def check_snap_info(self, error, state, ignore_snap_errors, version = 0):
        snapdev = elastio_snap.info(self.minor)
        self.assertIsNotNone(snapdev)

        self.assertEqual(snapdev["error"], error)
        self.assertEqual(snapdev["state"], state)
        self.assertEqual(snapdev["cow"], "/{}".format(self.cow_file))
        self.assertEqual(snapdev["bdev"], self.device)
        self.assertEqual(snapdev["version"], version)
        self.assertEqual(snapdev["ignore_snap_errors"], ignore_snap_errors)
        if  state & elastio_snap.State.ACTIVE:
            self.assertGreater(snapdev["falloc_size"], 0)
        else:
            self.assertEqual(snapdev["falloc_size"], 0)


if __name__ == "__main__":
    unittest.main()
