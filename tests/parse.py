#!/usr/bin/env python

#
# Copyright (C) 2017 Datto, Inc.
#
# This file is part of dattobd.
#
# This file is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public
# License v2 as published by the Free Software Foundation.
#
# This file is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
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
