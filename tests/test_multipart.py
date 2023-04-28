#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only

#
# Copyright (C) 2022 Elastio Software Inc.
#

import util
import math
import errno
import os
import platform
import subprocess
import time
import unittest
from random import randint

import elastio_snap
import util
from devicetestcase_multipart import DeviceTestCaseMultipart

class TestMultipart(DeviceTestCaseMultipart):
    def setUp(self):
        self.cow_file = "cow.snap"
        self.cow_full_paths = []
        self.snap_devices = []
        self.snap_mounts = []
        for i in range(self.part_count):
            self.cow_full_paths.append("{}/{}".format(self.mounts[i], self.cow_file))
            self.snap_devices.append("/dev/elastio-snap{}".format(self.minors[i]))
            self.snap_mounts.append("/tmp/elio-snap-mnt{0:03d}".format(self.minors[i]))
            os.makedirs(self.snap_mounts[i], exist_ok=True)
            self.addCleanup(os.rmdir, self.snap_mounts[i])

        util.test_track(self._testMethodName, started=True)

    def tearDown(self):
        util.test_track(self._testMethodName, started=False)

    def test_multipart_setup_volumes_same_disk(self):
        # Setup snapshot devices and check them
        for i in range(self.part_count):
            self.assertEqual(elastio_snap.setup(self.minors[i], self.devices[i], self.cow_full_paths[i]), 0)

            self.assertTrue(os.path.exists(self.snap_devices[i]))

            snapdev = elastio_snap.info(self.minors[i])
            self.assertIsNotNone(snapdev)

            self.assertEqual(snapdev["error"], 0)
            self.assertEqual(snapdev["state"], elastio_snap.State.ACTIVE | elastio_snap.State.SNAPSHOT)
            self.assertEqual(snapdev["cow"], "/{}".format(self.cow_file))
            self.assertEqual(snapdev["bdev"], self.devices[i])
            self.assertEqual(snapdev["version"], 1)

        # Destroy snapshot devices
        for i in reversed(range(self.part_count)):
            self.assertEqual(elastio_snap.destroy(self.minors[i]), 0)


    def test_multipart_setup_volumes_write_last_after_destroy(self):
        if (self.part_count < 2):
            self.skipTest("This test requires at least 2 partitions")

        for i in range(self.part_count):
            self.assertEqual(elastio_snap.setup(self.minors[i], self.devices[i], self.cow_full_paths[i]), 0)
            self.assertTrue(os.path.exists(self.snap_devices[i]))

        # Destroy last snapshot device first
        self.assertEqual(elastio_snap.destroy(self.minors[-1]), 0)

        # Write to the last device
        testfile = "{}/testfile".format(self.mounts[-1])
        with open(testfile, "w") as f:
            f.write("The quick brown fox")

        self.addCleanup(os.remove, testfile)
        os.sync()
        # Wait a bit for the panic? No?
        time.sleep(1)

        # Destroy remaining snapshot devices, finally, from the last to the first
        for i in reversed(range(self.part_count - 1)):
            self.assertEqual(elastio_snap.destroy(self.minors[i]), 0)


    @unittest.skipIf(os.getenv('TEST_FS') == "xfs" and  platform.release().rsplit(".", 1)[0] == "4.18.0-383.el8", "Broken on CentOS 8 with kernel 4.18.0-383.el8 and XFS. See #159")
    def test_multipart_modify_origins(self):
        for i in range(self.part_count):
            dev_size_mb = util.dev_size_mb(self.devices[i])

            # The goal of this test is to ensure the data integrity

            # We subtract a couple of megabytes to make sure the cow
            # file won't overflow during the test
            file_size_mb = math.floor(dev_size_mb * 0.1) - 2

            testfile = "{}/testfile".format(self.mounts[i])
            snapfile = "{}/testfile".format(self.snap_mounts[i])

            util.dd("/dev/urandom", testfile, file_size_mb, bs="1M")
            os.sync()

            self.addCleanup(os.remove, testfile)
            md5_orig = util.md5sum(testfile)

            self.assertEqual(elastio_snap.setup(self.minors[i], self.devices[i], self.cow_full_paths[i]), 0)
            self.addCleanup(elastio_snap.destroy, self.minors[i])

            util.dd("/dev/urandom", testfile, file_size_mb, bs="1M")
            os.sync()

            # TODO: norecovery option, probably, should not be here after the fix of the elastio/elastio-snap#63
            opts = "nouuid,norecovery,ro" if (self.fs == "xfs") else "ro"
            util.mount(self.snap_devices[i], self.snap_mounts[i], opts)
            self.addCleanup(util.unmount, self.snap_mounts[i])

            md5_snap = util.md5sum(snapfile)
            self.assertEqual(md5_orig, md5_snap)

if __name__ == "__main__":
    unittest.main()
