#!/usr/bin/env python
# SPDX-License-Identifier: GPL-2.0-only

#
# Copyright (C) 2017 Datto, Inc.
#

from __future__ import print_function

import json
import os
import sys

def parse(minor, field):
    with open("/proc/datto-info") as f:
        j = json.load(f)

    for d in j["devices"]:
        # Look for the object with the minor we want.
        if d["minor"] != int(minor):
            continue

        if field in d:
            print(d[field])
            return 0

        # Special case: Error field is not printed if there is none.
        if field == "error":
            print("0")
            return 0

    # Minor of field doesn't exist.
    return 1

if __name__ == "__main__":
    minor = sys.argv[1]
    field = sys.argv[2]

    sys.exit(parse(minor, field))
