#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only

#
# Copyright (C) 2019 Datto, Inc.
# Additional contributions by Elastio Software, Inc are Copyright (C) 2020 Elastio Software Inc.
#

import errno
import os
import platform
import unittest

import elastio_snap
import util
from devicetestcase import DeviceTestCase


class TestTransitionToIncremental(DeviceTestCase):
    def setUp(self):
        self.cow_file = "cow.snap"
        self.cow_full_path = "{}/{}".format(self.mount, self.cow_file)

    def test_transition_nonexistent_snapshot(self):
        self.assertIsNone(elastio_snap.info(self.minor))
        self.assertEqual(elastio_snap.transition_to_incremental(self.minor), errno.ENOENT)

    def test_transition_active_snapshot(self):
        self.assertEqual(elastio_snap.setup(self.minor, self.device, self.cow_full_path), 0)
        self.addCleanup(elastio_snap.destroy, self.minor)

        self.assertEqual(elastio_snap.transition_to_incremental(self.minor), 0)

    def test_transition_active_incremental(self):
        self.assertEqual(elastio_snap.setup(self.minor, self.device, self.cow_full_path), 0)
        self.addCleanup(elastio_snap.destroy, self.minor)

        self.assertEqual(elastio_snap.transition_to_incremental(self.minor), 0)
        self.assertEqual(elastio_snap.transition_to_incremental(self.minor), errno.EINVAL)

    def test_transition_fs_sync_cow_full(self):
        scratch = "{}/scratch".format(self.mount)
        falloc = 50

        self.assertEqual(elastio_snap.setup(self.minor, self.device, self.cow_full_path, fallocated_space=falloc), 0)
        self.addCleanup(elastio_snap.destroy, self.minor)

        util.dd("/dev/zero", scratch, falloc + 10, bs="1M")
        self.addCleanup(os.remove, scratch)

        # Possible errors doing this:
        # * EINVAL: The file system already performed the sync
        # * EFBIG: The module performed the sync
        # We want the former to happen, so make the OS sync everything.

        os.sync()
        err = elastio_snap.transition_to_incremental(self.minor)
        self.assertTrue(err == errno.EINVAL or err == errno.EFBIG)

        snapdev = elastio_snap.info(self.minor)
        self.assertIsNotNone(snapdev)

        self.assertEqual(snapdev["error"], -errno.EFBIG)
        self.assertEqual(snapdev["state"], elastio_snap.State.ACTIVE | elastio_snap.State.SNAPSHOT)
        self.assertTrue(snapdev["flags"] & elastio_snap.Flags.COW_ON_BDEV)

    def test_transition_mod_sync_cow_full(self):
        scratch = "{}/scratch".format(self.mount)
        falloc = 50

        self.assertEqual(elastio_snap.setup(self.minor, self.device, self.cow_full_path, fallocated_space=falloc), 0)
        self.addCleanup(elastio_snap.destroy, self.minor)

        util.dd("/dev/zero", scratch, falloc + 10, bs="1M")
        self.addCleanup(os.remove, scratch)

        # Possible errors doing this:
        # * EINVAL: The file system already performed the sync
        # * EFBIG: The module performed the sync
        # We want the later to happen, so try to transition without calling sync.

        err = elastio_snap.transition_to_incremental(self.minor)
        if (err != errno.EFBIG):
            self.skipTest("Kernel flushed before module")

        snapdev = elastio_snap.info(self.minor)
        self.assertIsNotNone(snapdev)

        self.assertEqual(snapdev["error"], -errno.EFBIG)
        self.assertEqual(snapdev["state"], elastio_snap.State.ACTIVE)
        self.assertTrue(snapdev["flags"] & elastio_snap.Flags.COW_ON_BDEV)


if __name__ == "__main__":
    unittest.main()
