#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only

#
# Copyright (C) 2023 Elastio Software
#

import math
import errno
import os
import platform
import unittest

import elastio_snap
import util
from devicetestcase import DeviceTestCase


class TestUpdateImage(DeviceTestCase):
    def setUp(self):
        self.snap_device = "/dev/elastio-snap{}".format(self.minor)
        self.snap_bkp = "./backup.img"

        util.test_track(self._testMethodName, started=True)

    def tearDown(self):
        util.test_track(self._testMethodName, started=False)

    @unittest.skipIf(platform.release() == "4.18.0-408.el8.aarch64", "Broken on CentOS 8 with kernel 4.18.0-408.el8.aarch64. See #266")
    def test_update_sequence(self):
        iterations = 25
        file_name = "testfile"
        cow_paths = ["{}/{}".format(self.mount, "cow{}".format(i)) for i in range(0, iterations)]

        self.assertEqual(elastio_snap.setup(self.minor, self.device, cow_paths[0]), 0)
        self.addCleanup(elastio_snap.destroy, self.minor)

        # preparing base image
        util.dd(self.snap_device, self.snap_bkp, self.size_mb, bs="1M")
        self.addCleanup(os.remove, self.snap_bkp)
        write_testfile = "{}/{}".format(self.mount, file_name)

        for i in range(1, iterations):
            with open(write_testfile, "a") as f:
                f.write("Attempt to save humanity #{}\n".format(i))

            self.assertEqual(elastio_snap.transition_to_incremental(self.minor), 0)
            self.assertEqual(elastio_snap.transition_to_snapshot(self.minor, cow_paths[i]), 0)

            os.sync()

            util.update_img(self.snap_device, cow_paths[i - 1], self.snap_bkp)
            os.remove(cow_paths[i - 1])

        temp_dir = util.mktemp_dir()
        self.addCleanup(os.rmdir, temp_dir)

        loop_dev = util.loop_create(self.snap_bkp)
        self.addCleanup(util.loop_destroy, loop_dev)

        # Need to repair the xfs (see #63)
        if self.fs == 'xfs':
            util.mount(loop_dev, temp_dir, opts="nouuid")
            util.unmount(temp_dir)

        # For some reason, even valid xfs file system is shown
        # as 'not valid' with xfs_repair v4.9.0
        if util.xfs_repair_version() != '4.9.0':
            util.fsck(loop_dev, self.fs)

        read_testfile = "{}/{}".format(temp_dir, file_name)

        if self.fs == 'xfs':
            util.mount(loop_dev, temp_dir, opts="nouuid")
        else:
            util.mount(loop_dev, temp_dir)

        self.addCleanup(util.unmount, temp_dir)
        self.assertEqual(util.file_lines(read_testfile), iterations - 1)

 
if __name__ == "__main__":
    unittest.main()
