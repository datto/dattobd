#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only

#
# Copyright (C) 2019 Datto, Inc.
# Additional contributions by Elastio Software, Inc are Copyright (C) 2020 Elastio Software Inc.
#

import math
import errno
import os
import platform
import unittest

import elastio_snap
import util
from devicetestcase import DeviceTestCase


class TestSnapshot(DeviceTestCase):
    def setUp(self):
        self.cow_file = "cow.snap"
        self.cow_full_path = "{}/{}".format(self.mount, self.cow_file)

        self.snap_mount = "/mnt"
        self.snap_device = "/dev/elastio-snap{}".format(self.minor)

        util.test_track(self._testMethodName, started=True)

    def tearDown(self):
        util.test_track(self._testMethodName, started=False)

    def test_modify_origin(self):
        dev_size_mb = util.dev_size_mb(self.device)

        # The goal of this test is to ensure the data integrity
        # For regular devices, we fill the cow file almost completely;
        # For RAID based devices we need to leave a little bit more

        # We subtract a couple of megabytes to make sure the cow
        # file won't overflow during the test
        if self.is_raid == True:
            file_size_mb = math.floor(dev_size_mb * 0.06)
        else:
            file_size_mb = math.floor(dev_size_mb * 0.1) - 5

        testfile = "{}/testfile".format(self.mount)
        snapfile = "{}/testfile".format(self.snap_mount)

        util.dd("/dev/urandom", testfile, file_size_mb, bs="1M")
        os.sync()

        self.addCleanup(os.remove, testfile)
        md5_orig = util.md5sum(testfile)

        self.assertEqual(elastio_snap.setup(self.minor, self.device, self.cow_full_path), 0)
        self.addCleanup(elastio_snap.destroy, self.minor)

        util.dd("/dev/urandom", testfile, file_size_mb, bs="1M")
        os.sync()

        # TODO: norecovery option, probably, should not be here after the fix of the elastio/elastio-snap#63
        opts = "nouuid,norecovery,ro" if (self.fs == "xfs") else "ro"
        util.mount(self.snap_device, self.snap_mount, opts)
        self.addCleanup(util.unmount, self.snap_mount)

        md5_snap = util.md5sum(snapfile)
        self.assertEqual(md5_orig, md5_snap)

    def test_track_writes(self):
        testfile = "{}/testfile".format(self.mount)

        self.assertEqual(elastio_snap.setup(self.minor, self.device, self.cow_full_path), 0)
        self.addCleanup(elastio_snap.destroy, self.minor)

        os.sync()
        info = elastio_snap.info(self.minor)
        start_nr = info["nr_changed_blocks"]

        with open(testfile, "w") as f:
            f.write("The quick brown fox")

        self.addCleanup(os.remove, testfile)
        os.sync()

        info = elastio_snap.info(self.minor)
        end_nr = info["nr_changed_blocks"]
        self.assertGreater(end_nr, start_nr)

    def test_next_available_minor(self):
        self.assertEqual(elastio_snap.get_free_minor(), 0)

        # Explicitly use a minor of 0 for testing this function
        self.assertEqual(elastio_snap.setup(0, self.device, self.cow_full_path), 0)
        self.addCleanup(elastio_snap.destroy, 0)

        self.assertEqual(elastio_snap.get_free_minor(), 1)


    def test_cow_not_deleteable(self):
        self.assertEqual(elastio_snap.setup(self.minor, self.device, self.cow_full_path), 0)
        self.addCleanup(elastio_snap.destroy, self.minor)

        try:
            os.remove(self.cow_full_path)
        except OSError as e:
            self.assertEqual(e.errno, errno.EPERM)
        else:
            self.fail("file is not immutable")


    def test_cow_not_movable(self):
        self.assertEqual(elastio_snap.setup(self.minor, self.device, self.cow_full_path), 0)
        self.addCleanup(elastio_snap.destroy, self.minor)

        try:
            os.replace(self.cow_full_path, '{}/{}'.format(self.mount, 'test.cow'))
        except OSError as e:
            self.assertEqual(e.errno, errno.EPERM)
        else:
            self.fail("file is not immutable")


if __name__ == "__main__":
    unittest.main()
