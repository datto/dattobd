#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only

#
# Copyright (C) 2022 Elastio Software Inc.
#

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

class TestStorageRedirected(DeviceTestCaseMultipart):
    def setUp(self):
        self.source_part_num = 0
        self.target_part_num = 1

        self.cow_file = "cow.snap"
        self.minor = self.minors[self.source_part_num]
        self.device = self.devices[self.source_part_num]
        self.source_mount = self.mounts[self.source_part_num]
        self.target_mount = self.mounts[self.target_part_num]
        self.target_device = self.devices[self.target_part_num]
        self.cow_full_path = "{}/{}".format(self.target_mount, self.cow_file)

        self.snap_device = "/dev/elastio-snap{}".format(self.minor)
        self.snap_mount = "/tmp/elio-snap-mnt{0:03d}".format(self.minor)
        os.makedirs(self.snap_mount, exist_ok=True)
        self.addCleanup(os.rmdir, self.snap_mount)

    def test_redirected_setup_snapshot(self):
        self.assertEqual(elastio_snap.setup(self.minor, self.device, self.cow_full_path), 0)
        self.addCleanup(elastio_snap.destroy, self.minor)
        self.assertTrue(os.path.exists(self.snap_device))

        snapdev = elastio_snap.info(self.minor)
        self.assertIsNotNone(snapdev)
        self.assertFalse(snapdev["flags"] & elastio_snap.Flags.COW_ON_BDEV)
        self.assertEqual(snapdev["cow"], self.cow_full_path)

        self.assertEqual(elastio_snap.destroy(self.minor), 0)
        self.assertFalse(os.path.exists(self.snap_device))
        self.assertIsNone(elastio_snap.info(self.minor))
        self.assertFalse(os.path.exists(self.cow_full_path))

    def test_redirected_setup_incremental(self):
        self.assertEqual(elastio_snap.setup(self.minor, self.device, self.cow_full_path), 0)
        self.addCleanup(elastio_snap.destroy, self.minor)
        self.assertTrue(os.path.exists(self.snap_device))

        snapdev = elastio_snap.info(self.minor)
        self.assertIsNotNone(snapdev)
        self.assertFalse(snapdev["flags"] & elastio_snap.Flags.COW_ON_BDEV)
        self.assertEqual(snapdev["cow"], self.cow_full_path)
        self.assertEqual(snapdev["state"], elastio_snap.State.ACTIVE | elastio_snap.State.SNAPSHOT)

        self.assertEqual(elastio_snap.transition_to_incremental(self.minor), 0)
        snapdev = elastio_snap.info(self.minor)
        self.assertIsNotNone(snapdev)
        self.assertFalse(snapdev["flags"] & elastio_snap.Flags.COW_ON_BDEV)
        self.assertEqual(snapdev["cow"], self.cow_full_path)
        self.assertEqual(snapdev["state"], elastio_snap.State.ACTIVE)

        new_cow_on_bdev = "{}/{}".format(self.source_mount, self.cow_file)
        self.assertEqual(elastio_snap.transition_to_snapshot(self.minor, new_cow_on_bdev), 0)
        snapdev = elastio_snap.info(self.minor)
        self.assertIsNotNone(snapdev)
        self.assertEqual(snapdev["flags"], elastio_snap.Flags.COW_ON_BDEV)
        self.assertEqual(snapdev["cow"], "/{}".format(self.cow_file))

    def test_redirected_modify_origin_snap(self):
        testfile = "{}/testfile".format(self.source_mount)
        snapfile = "{}/testfile".format(self.snap_mount)

        with open(testfile, "w") as f:
            f.write("The quick brown fox")

        self.addCleanup(os.remove, testfile)
        os.sync()
        md5_orig = util.md5sum(testfile)

        self.assertEqual(elastio_snap.setup(self.minor, self.device, self.cow_full_path), 0)
        self.addCleanup(elastio_snap.destroy, self.minor)

        with open(testfile, "w") as f:
            f.write("jumps over the lazy dog")

        os.sync()
        # TODO: norecovery option, probably, should not be here after the fix of the elastio/elastio-snap#63
        opts = "nouuid,norecovery,ro" if (self.fs == "xfs") else "ro"
        util.mount(self.snap_device, self.snap_mount, opts)
        self.addCleanup(util.unmount, self.snap_mount)

        md5_snap = util.md5sum(snapfile)
        self.assertEqual(md5_orig, md5_snap)

    @unittest.skipIf(os.getenv('TEST_FS') == "ext2" and int(platform.release().split(".", 1)[0]) < 4, "Broken on ext2, 3-rd kernels")
    @unittest.skipIf(os.getenv('TEST_FS') == "xfs", "Broken on XFS, due to ignored os.sync and due to #63.")
    def test_redirected_modify_origin_incremental(self):
        testfile = "{}/testfile".format(self.source_mount)
        snapfile = "{}/testfile".format(self.snap_mount)

        with open(testfile, "w") as f:
            f.write("The quick brown fox")

        self.addCleanup(os.remove, testfile)
        os.sync()
        md5_orig = util.md5sum(testfile)

        self.assertEqual(elastio_snap.setup(self.minor, self.device, self.cow_full_path), 0)
        self.addCleanup(elastio_snap.destroy, self.minor)
        self.assertEqual(elastio_snap.transition_to_incremental(self.minor), 0)

        info = elastio_snap.info(self.minor)
        start_nr = info["nr_changed_blocks"]

        with open(testfile, "w") as f:
            f.write("jumps over the lazy dog")

        os.sync()

        info = elastio_snap.info(self.minor)
        end_nr = info["nr_changed_blocks"]
        self.assertGreater(end_nr, start_nr)

    def test_redirected_remount_active_snapshot(self):
        self.assertEqual(elastio_snap.setup(self.minor, self.device, self.cow_full_path), 0)
        self.addCleanup(elastio_snap.destroy, self.minor)

        util.unmount(self.source_mount)
        self.addCleanup(util.mount, self.device, self.source_mount)

        snapdev = elastio_snap.info(self.minor)
        self.assertIsNotNone(snapdev)
        self.assertEqual(snapdev["error"], 0)
        self.assertEqual(snapdev["state"], elastio_snap.State.SNAPSHOT)
        self.assertEqual(snapdev["cow"], self.cow_full_path)
        self.assertEqual(snapdev["bdev"], self.device)
        self.assertEqual(snapdev["version"], 1)
        self.assertNotEqual(snapdev["falloc_size"], 0)
        self.assertFalse(snapdev["flags"] & elastio_snap.Flags.COW_ON_BDEV)

        # Mount and test that snapshot is active
        util.mount(self.device, self.source_mount)
        self.addCleanup(util.unmount, self.device, self.source_mount)

        self.assertTrue(os.path.exists(self.cow_full_path))
        self.assertTrue(os.path.exists(self.snap_device))

        snapdev = elastio_snap.info(self.minor)
        self.assertIsNotNone(snapdev)
        self.assertEqual(snapdev["error"], 0)
        self.assertEqual(snapdev["state"], elastio_snap.State.ACTIVE | elastio_snap.State.SNAPSHOT)
        self.assertEqual(snapdev["cow"], self.cow_full_path)
        self.assertEqual(snapdev["bdev"], self.device)
        self.assertEqual(snapdev["version"], 1)
        self.assertNotEqual(snapdev["falloc_size"], 0)
        self.assertFalse(snapdev["flags"] & elastio_snap.Flags.COW_ON_BDEV)

    def test_redirected_remount_active_incremental(self):
        self.assertEqual(elastio_snap.setup(self.minor, self.device, self.cow_full_path), 0)
        self.addCleanup(elastio_snap.destroy, self.minor)
        self.assertEqual(elastio_snap.transition_to_incremental(self.minor), 0)

        util.unmount(self.source_mount)
        self.addCleanup(util.mount, self.device, self.source_mount)

        snapdev = elastio_snap.info(self.minor)
        self.assertIsNotNone(snapdev)
        self.assertEqual(snapdev["error"], 0)
        self.assertEqual(snapdev["state"], 0)
        self.assertEqual(snapdev["cow"], self.cow_full_path)
        self.assertEqual(snapdev["bdev"], self.device)
        self.assertEqual(snapdev["version"], 1)
        self.assertNotEqual(snapdev["falloc_size"], 0)
        self.assertFalse(snapdev["flags"] & elastio_snap.Flags.COW_ON_BDEV)

        # Mount and test that snapshot is active
        util.mount(self.device, self.source_mount)
        self.addCleanup(util.unmount, self.device, self.source_mount)

        self.assertTrue(os.path.exists(self.cow_full_path))
        self.assertFalse(os.path.exists(self.snap_device))

        snapdev = elastio_snap.info(self.minor)
        self.assertIsNotNone(snapdev)
        self.assertEqual(snapdev["error"], 0)
        self.assertEqual(snapdev["state"], elastio_snap.State.ACTIVE)
        self.assertEqual(snapdev["cow"], self.cow_full_path)
        self.assertEqual(snapdev["bdev"], self.device)
        self.assertEqual(snapdev["version"], 1)
        self.assertNotEqual(snapdev["falloc_size"], 0)
        self.assertFalse(snapdev["flags"] & elastio_snap.Flags.COW_ON_BDEV)

    def test_redirected_reload_snapshot(self):
        self.assertEqual(elastio_snap.setup(self.minor, self.device, self.cow_full_path), 0)

        util.unmount(self.source_mount)
        self.addCleanup(util.mount, self.device, self.source_mount)

        self.kmod.unload()
        self.kmod.load()
        self.assertFalse(os.path.exists(self.snap_device))

        #NOTE: for redirected storage should be used full_path
        self.assertEqual(elastio_snap.reload_snapshot(self.minor, self.device, self.cow_full_path), 0)
        self.addCleanup(elastio_snap.destroy, self.minor)
        self.assertFalse(os.path.exists(self.snap_device))

        snapdev = elastio_snap.info(self.minor)
        self.assertIsNotNone(snapdev)

        self.assertEqual(snapdev["error"], 0)
        self.assertEqual(snapdev["state"], elastio_snap.State.UNVERIFIED | elastio_snap.State.SNAPSHOT)
        self.assertEqual(snapdev["cow"], self.cow_full_path)
        self.assertEqual(snapdev["bdev"], self.device)
        self.assertEqual(snapdev["version"], 0)
        self.assertEqual(snapdev["falloc_size"], 0)
        self.assertFalse(snapdev["flags"] & elastio_snap.Flags.COW_ON_BDEV)

        # Mount and test that snapshot is active
        util.mount(self.device, self.source_mount)
        self.addCleanup(util.unmount, self.device, self.source_mount)

        self.assertTrue(os.path.exists(self.cow_full_path))
        self.assertTrue(os.path.exists(self.snap_device))

        snapdev = elastio_snap.info(self.minor)
        self.assertIsNotNone(snapdev)

        self.assertEqual(snapdev["error"], 0)
        self.assertEqual(snapdev["state"], elastio_snap.State.ACTIVE | elastio_snap.State.SNAPSHOT)
        self.assertEqual(snapdev["cow"], self.cow_full_path)
        self.assertEqual(snapdev["bdev"], self.device)
        self.assertEqual(snapdev["version"], 1)
        self.assertNotEqual(snapdev["falloc_size"], 0)
        self.assertFalse(snapdev["flags"] & elastio_snap.Flags.COW_ON_BDEV)

    def test_redirected_reload_incremental(self):
        self.assertEqual(elastio_snap.setup(self.minor, self.device, self.cow_full_path), 0)
        self.assertEqual(elastio_snap.transition_to_incremental(self.minor), 0)

        util.unmount(self.source_mount)
        self.addCleanup(util.mount, self.device, self.source_mount)

        self.kmod.unload()
        self.kmod.load()
        self.assertFalse(os.path.exists(self.snap_device))

        #NOTE: for redirected storage should be used full_path
        self.assertEqual(elastio_snap.reload_incremental(self.minor, self.device, self.cow_full_path), 0)
        self.addCleanup(elastio_snap.destroy, self.minor)
        self.assertFalse(os.path.exists(self.snap_device))

        snapdev = elastio_snap.info(self.minor)
        self.assertIsNotNone(snapdev)

        self.assertEqual(snapdev["error"], 0)
        self.assertEqual(snapdev["state"], elastio_snap.State.UNVERIFIED)
        self.assertEqual(snapdev["cow"], self.cow_full_path)
        self.assertEqual(snapdev["bdev"], self.device)
        self.assertEqual(snapdev["version"], 0)
        self.assertEqual(snapdev["falloc_size"], 0)

        # Mount and test that snapshot is active
        util.mount(self.device, self.source_mount)
        self.addCleanup(util.unmount, self.device, self.source_mount)

        self.assertTrue(os.path.exists(self.cow_full_path))
        self.assertFalse(os.path.exists(self.snap_device))

        snapdev = elastio_snap.info(self.minor)
        self.assertIsNotNone(snapdev)

        self.assertEqual(snapdev["error"], 0)
        self.assertEqual(snapdev["state"], elastio_snap.State.ACTIVE)
        self.assertEqual(snapdev["cow"], self.cow_full_path)
        self.assertEqual(snapdev["bdev"], self.device)
        self.assertEqual(snapdev["version"], 1)
        self.assertNotEqual(snapdev["falloc_size"], 0)

    def test_redirected_umount_target_snapshot(self):
        self.assertEqual(elastio_snap.setup(self.minor, self.device, self.cow_full_path), 0)
        self.addCleanup(elastio_snap.destroy, self.minor)
        self.addCleanup(util.mount, self.target_device, self.target_mount)

        #NOTE: Target is busy until snapshot exists
        self.assertRaises(subprocess.CalledProcessError, util.unmount, self.target_mount)

        self.assertEqual(elastio_snap.destroy(self.minor), 0)
        util.unmount(self.target_mount)

    def test_redirected_umount_target_incremental(self):
        self.assertEqual(elastio_snap.setup(self.minor, self.device, self.cow_full_path), 0)
        self.addCleanup(elastio_snap.destroy, self.minor)
        self.assertEqual(elastio_snap.transition_to_incremental(self.minor), 0)

        self.addCleanup(util.mount, self.target_device, self.target_mount)

        #NOTE: Target is busy until incremental exists
        self.assertRaises(subprocess.CalledProcessError, util.unmount, self.target_mount)

        self.assertEqual(elastio_snap.destroy(self.minor), 0)
        util.unmount(self.target_mount)

    def test_redirected_umount_source_snapshot(self):
        self.assertEqual(elastio_snap.setup(self.minor, self.device, self.cow_full_path), 0)
        self.addCleanup(elastio_snap.destroy, self.minor)

        info = elastio_snap.info(self.minor)
        self.assertEqual(info["state"], elastio_snap.State.ACTIVE | elastio_snap.State.SNAPSHOT)

        util.unmount(self.source_mount)
        self.addCleanup(util.mount, self.device, self.source_mount)

        info = elastio_snap.info(self.minor)
        self.assertEqual(info["state"], elastio_snap.State.SNAPSHOT)

        os.remove(self.cow_full_path) #NOTE: should not raise an exception
        self.assertFalse(os.path.exists(self.cow_full_path))

        self.assertEqual(elastio_snap.destroy(self.minor), 0)

        self.assertFalse(os.path.exists(self.snap_device))
        self.assertIsNone(elastio_snap.info(self.minor))

    def test_redirected_umount_source_incremental(self):
        self.assertEqual(elastio_snap.setup(self.minor, self.device, self.cow_full_path), 0)
        self.addCleanup(elastio_snap.destroy, self.minor)
        self.assertEqual(elastio_snap.transition_to_incremental(self.minor), 0)

        info = elastio_snap.info(self.minor)
        self.assertEqual(info["state"], elastio_snap.State.ACTIVE)

        util.unmount(self.source_mount)
        self.addCleanup(util.mount, self.device, self.source_mount)

        info = elastio_snap.info(self.minor)
        self.assertEqual(info["state"], 0)

        os.remove(self.cow_full_path) #NOTE: should not raise an exception
        self.assertFalse(os.path.exists(self.cow_full_path))

        self.assertEqual(elastio_snap.destroy(self.minor), 0)

        self.assertFalse(os.path.exists(self.snap_device))
        self.assertIsNone(elastio_snap.info(self.minor))


if __name__ == "__main__":
    unittest.main()
