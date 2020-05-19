#!/bin/bash

dir=$(readlink -f $(dirname $0))
out=$dir/artifacts

declare -A COPY_DIRS
COPY_DIRS[rpm]="RPMS/x86_64 RPMS/noarch SRPMS"
COPY_DIRS[deb]="DEBS/**"

declare -A POOLS
POOLS[rpm]="x86_64/Packages"
POOLS[deb]="pool"

# system-release should be last, it's present on all rpm systems
# and it's used here for amazon linux detection
declare -a RELEASE_FILES=("centos-release" "fedora-release" "debian_version" "system-release")

# Make 'info' file with the artifacts details
make_info () {
if [ -z "$DRONE_BUILD_NUMBER" ]; then
    DRONE_BUILD_NUMBER='local_build'
    DRONE_SOURCE_BRANCH=`git rev-parse --abbrev-ref HEAD`
    DRONE_COMMIT=`git rev-parse --short HEAD`
fi

mkdir -p $out/Debian
cat > $out/Debian/elastio-snap.info << INFO
Branch:     $DRONE_SOURCE_BRANCH
Rev:        $DRONE_COMMIT
Version:    `grep Version: $dir/../dist/elastio-snap.spec | awk '{print $NF}'`
Build:      $DRONE_BUILD_NUMBER
INFO
}

# Expected dist_name-s: Amazon CentOS Fedora
dist_name=`cat /etc/system-release 2>/dev/null | cut -d' ' -f1`
dist_ver=
pkg_type=rpm
for file in "${RELEASE_FILES[@]}"; do
    if [ -f "/etc/$file" ]; then
        # Expected dist_name-s: Debian CentOS Fedora
        [ -z "$dist_name" ] && dist_name=`d=${file^} && echo ${d//os/OS} | cut -d'-' -f1 | cut -d'_' -f1`
        dist_ver=`cat /etc/$file | tr -cd '[0-9.]' | cut -d'.' -f1`
        [ "$file" == "debian_version" ] && pkg_type="deb" && make_info
        break
    fi
done

if [ -z $dist_ver ] || [ -z $dist_name ]; then
    echo "Unknown Linux distro"
    exit 1
fi

pool=$out/$dist_name/$dist_ver/${POOLS[$pkg_type]}
copy_dirs=(${COPY_DIRS[$pkg_type]})

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
