# SPDX-License-Identifier: GPL-2.0-only

#
# Copyright (C) 2019 Datto, Inc.
# Additional contributions by Elastio Software, Inc are Copyright (C) 2020 Elastio Software Inc.
#

import os
import unittest

import kmod
import util

from random import randint

@unittest.skipUnless(os.geteuid() == 0, "Must be run as root")
class DeviceTestCase(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.minor = randint(0, 23)
        r =  randint(0, 999)
        cls.backing_store = "/tmp/disk_{0:03d}.img".format(r)
        cls.mount = "/tmp/elastio-snap_{0:03d}".format(r)

        cls.kmod = kmod.Module("../src/elastio-snap.ko")
        cls.kmod.load(debug=1)

        util.dd("/dev/zero", cls.backing_store, 256, bs="1M")
        cls.device = util.loop_create(cls.backing_store)

        util.mkfs(cls.device)
        os.makedirs(cls.mount, exist_ok=True)
        util.mount(cls.device, cls.mount)

    @classmethod
    def tearDownClass(cls):
        util.unmount(cls.mount)
        util.loop_destroy(cls.device)
        os.unlink(cls.backing_store)
        cls.kmod.unload()
