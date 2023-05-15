#!/bin/bash

# After introducing changes with ftrace there was an issue with calling module-exit during shutdown.
# It looked like it is not called. The aim of this script is to call rmmod manually after unmounting the filesystem.

cp bin/call-rmmod-after-umount.sh /bin/call-rmmod-after-umount.sh
mv /bin/umount /bin/umount.real
chmod +x /bin/call-rmmod-after-umount.sh
ln -s /bin/call-rmmod-after-umount.sh /bin/umount