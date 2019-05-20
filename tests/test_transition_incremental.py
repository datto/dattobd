#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only

#
# Copyright (C) 2019 Datto, Inc.
#

import errno
import os
import unittest

import dattobd
import util
from devicetestcase import DeviceTestCase


class TestTransitionToIncremental(DeviceTestCase):
    def setUp(self):
        self.device = "/dev/loop0"
        self.mount = "/tmp/dattobd"
        self.cow_file = "cow.snap"
        self.cow_full_path = "{}/{}".format(self.mount, self.cow_file)
        self.minor = 1

    def test_transition_nonexistent_snapshot(self):
        self.assertIsNone(dattobd.info(self.minor))
        self.assertEqual(dattobd.transition_to_incremental(self.minor), errno.ENOENT)

    def test_transition_active_snapshot(self):
        self.assertEqual(dattobd.setup(self.minor, self.device, self.cow_full_path), 0)
        self.addCleanup(dattobd.destroy, self.minor)

        self.assertEqual(dattobd.transition_to_incremental(self.minor), 0)

    def test_transition_active_incremental(self):
        self.assertEqual(dattobd.setup(self.minor, self.device, self.cow_full_path), 0)
        self.addCleanup(dattobd.destroy, self.minor)

        self.assertEqual(dattobd.transition_to_incremental(self.minor), 0)
        self.assertEqual(dattobd.transition_to_incremental(self.minor), errno.EINVAL)

    def test_transition_fs_sync_cow_full(self):
        scratch = "{}/scratch".format(self.mount)
        falloc = 50

        self.assertEqual(dattobd.setup(self.minor, self.device, self.cow_full_path, fallocated_space=falloc), 0)
        self.addCleanup(dattobd.destroy, self.minor)

        util.dd("/dev/zero", scratch, falloc + 10, bs="1M")
        self.addCleanup(os.remove, scratch)

        # Possible errors doing this:
        # * EINVAL: The file system already performed the sync
        # * EFBIG: The module performed the sync
        # We want the former to happen, so make the OS sync everything.

        os.sync()
        self.assertEqual(dattobd.transition_to_incremental(self.minor), errno.EINVAL)

        snapdev = dattobd.info(self.minor)
        self.assertIsNotNone(snapdev)

        self.assertEqual(snapdev["error"], -errno.EFBIG)
        self.assertEqual(snapdev["state"], 3)

    def test_transition_mod_sync_cow_full(self):
        scratch = "{}/scratch".format(self.mount)
        falloc = 50

        self.assertEqual(dattobd.setup(self.minor, self.device, self.cow_full_path, fallocated_space=falloc), 0)
        self.addCleanup(dattobd.destroy, self.minor)

        util.dd("/dev/zero", scratch, falloc + 10, bs="1M")
        self.addCleanup(os.remove, scratch)

        # Possible errors doing this:
        # * EINVAL: The file system already performed the sync
        # * EFBIG: The module performed the sync
        # We want the later to happen, so try to transition without calling sync.

        err = dattobd.transition_to_incremental(self.minor)
        if (err != errno.EFBIG):
            self.skipTest("Kernel flushed before module")

        snapdev = dattobd.info(self.minor)
        self.assertIsNotNone(snapdev)

        self.assertEqual(snapdev["error"], -errno.EFBIG)
        self.assertEqual(snapdev["state"], 2)


if __name__ == "__main__":
    unittest.main()
