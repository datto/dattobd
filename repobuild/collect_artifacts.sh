#!/bin/bash

dir=$(readlink -f $(dirname $0))
out=$dir/artifacts

# Make 'info' file with the artifacts details
make_info () {
if [ -z "$DRONE_BUILD_NUMBER" ]; then
    DRONE_BUILD_NUMBER='local_build'
    DRONE_SOURCE_BRANCH=`git rev-parse --abbrev-ref HEAD`
    DRONE_COMMIT=`git rev-parse --short HEAD`
fi

mkdir -p $out/Debian
cat > $out/Debian/assurio-snap.info << INFO
Branch:     $DRONE_SOURCE_BRANCH
Rev:        $DRONE_COMMIT
Version:    `grep Version: $dir/../dist/assurio-snap.spec | awk '{print $NF}'`
Build:      $DRONE_BUILD_NUMBER
INFO
}

if [ -f "/etc/redhat-release" ]; then
    dist_ver=`cat /etc/redhat-release | tr -cd [0-9.] | cut -d'.' -f1`
    pool=$out/CentOS/$dist_ver/x86_64/Packages
    copy_dirs=(RPMS/{x86_64,noarch} SRPMS)
elif [ -f "/etc/debian_version" ]; then
    pool=$out/Debian/pool
    copy_dirs="DEBS/**"
    make_info
else
    echo "Unknown Linux distro"
    exit 1
fi

# Cleanup and prepare dir structure
rm -rf $pool
mkdir -p $pool

# Collect artifacts
for d in "${copy_dirs[@]}"; do
    echo $d
    cp -r $dir/../pkgbuild/$d/* $pool
done


# Show tree to the log
tree -h $out
