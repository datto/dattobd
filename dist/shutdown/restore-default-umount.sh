#!/bin/bash

# Clean setup-module-exit-after-umount

rm /bin/umount
mv /bin/umount.real /bin/umount
