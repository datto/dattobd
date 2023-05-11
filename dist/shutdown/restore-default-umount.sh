#!/bin/bash

# Clean setup-module-exit-after-umount

rm umount
rm /bin/call-rmmod-after-umount.sh
mv umount.real umount
