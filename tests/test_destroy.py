#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only

#
# Copyright (C) 2019 Datto, Inc.
# Additional contributions by Elastio Software, Inc are Copyright (C) 2020 Elastio Software Inc.
#

import errno
import os
import unittest
import platform
import elastio_snap
import util
from devicetestcase import DeviceTestCase


class TestDestroy(DeviceTestCase):
    def setUp(self):
        self.cow_file = "cow.snap"
        self.cow_full_path = "{}/{}".format(self.mount, self.cow_file)
        self.cow_reload_path = "/{}".format(self.cow_file)
        self.snap_device = "/dev/elastio-snap{}".format(self.minor)

    def test_destroy_nonexistent_device(self):
        self.assertEqual(elastio_snap.destroy(self.minor), errno.ENOENT)

    def test_destroy_active_snapshot(self):
        self.assertEqual(elastio_snap.setup(self.minor, self.device, self.cow_full_path), 0)

        self.assertEqual(elastio_snap.destroy(self.minor), 0)
        self.assertFalse(os.path.exists(self.snap_device))
        self.assertIsNone(elastio_snap.info(self.minor))

    def test_destroy_active_incremental(self):
        self.assertEqual(elastio_snap.setup(self.minor, self.device, self.cow_full_path), 0)
        self.assertEqual(elastio_snap.transition_to_incremental(self.minor), 0)

        self.assertEqual(elastio_snap.destroy(self.minor), 0)
        self.assertFalse(os.path.exists(self.snap_device))
        self.assertIsNone(elastio_snap.info(self.minor))

    def test_destroy_dormant_snapshot(self):
        self.assertEqual(elastio_snap.setup(self.minor, self.device, self.cow_full_path), 0)

        util.unmount(self.mount)
        self.addCleanup(os.remove, self.cow_full_path)
        self.addCleanup(util.mount, self.device, self.mount)

        self.assertEqual(elastio_snap.destroy(self.minor), 0)
        self.assertFalse(os.path.exists(self.snap_device))
        self.assertIsNone(elastio_snap.info(self.minor))

    def test_destroy_dormant_incremental(self):
        self.assertEqual(elastio_snap.setup(self.minor, self.device, self.cow_full_path), 0)
        self.assertEqual(elastio_snap.transition_to_incremental(self.minor), 0)

        util.unmount(self.mount)
        self.addCleanup(os.remove, self.cow_full_path)
        self.addCleanup(util.mount, self.device, self.mount)

        self.assertEqual(elastio_snap.destroy(self.minor), 0)
        self.assertFalse(os.path.exists(self.snap_device))
        self.assertIsNone(elastio_snap.info(self.minor))

    def test_destroy_unverified_snapshot(self):
        util.unmount(self.mount)
        self.addCleanup(util.mount, self.device, self.mount)
        self.assertEqual(elastio_snap.reload_snapshot(self.minor, self.device, self.cow_reload_path), 0)

        self.assertEqual(elastio_snap.destroy(self.minor), 0)
        self.assertFalse(os.path.exists(self.snap_device))
        self.assertIsNone(elastio_snap.info(self.minor))

    def test_destroy_unverified_incremental(self):
        util.unmount(self.mount)
        self.addCleanup(util.mount, self.device, self.mount)
        self.assertEqual(elastio_snap.reload_incremental(self.minor, self.device, self.cow_reload_path), 0)

        self.assertEqual(elastio_snap.destroy(self.minor), 0)
        self.assertFalse(os.path.exists(self.snap_device))
        self.assertIsNone(elastio_snap.info(self.minor))


if __name__ == "__main__":
    unittest.main()
