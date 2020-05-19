# SPDX-License-Identifier: GPL-2.0-only

#
# Copyright (C) 2019 Datto, Inc.
# Additional contributions by Elastio Software, Inc are Copyright (C) 2020 Elastio Software Inc.
#

import errno
import os
import subprocess


class Module(object):
    def __init__(self, path):
        self.path = path
        self.name = self.path.split("/")[-1].split(".")[0]
        self.timeout = 10

        if not os.path.isfile(self.path):
            raise FileNotFoundError(errno.ENOENT, os.strerror(errno.ENOENT), self.path)

    def load(self, **kwargs):
        cmd = ["insmod", self.path]
        for k, v in kwargs.items():
            cmd.append("{}={}".format(k, v))

        subprocess.check_call(
            cmd,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            timeout=self.timeout)

    def unload(self):
        cmd = ["rmmod", self.name]
        subprocess.check_call(
            cmd,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            timeout=self.timeout)

    def info(self):
        cmd = ["modinfo", self.path]
        subprocess.check_call(
            cmd,
            timeout=self.timeout)
