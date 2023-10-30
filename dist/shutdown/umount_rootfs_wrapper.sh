#!/bin/bash

# Determine the distribution (for example, based on /etc/os-release)
if [ -f /etc/os-release ]; then
    . /etc/os-release
    DISTRIBUTION=$ID
else
    DISTRIBUTION="unknown"
fi

# Define the script paths based on the distribution
case $DISTRIBUTION in
    debian|ubuntu)
        SCRIPT_PATH="/lib/systemd/system-shutdown/umount_rootfs.shutdown"
        echo "deb"
        ;;
    centos|rhel)
        SCRIPT_PATH="/usr/lib/systemd/system-shutdown/umount_rootfs.shutdown"
        echo "rhel"
        ;;
    *)
        echo "Distribution not supported."
        exit 1
        ;;
esac

# Execute the script
if [ -f "$SCRIPT_PATH" ]; then
    bash "$SCRIPT_PATH"
else
    echo "Script not found for distribution: $DISTRIBUTION"
    exit 1
fi
