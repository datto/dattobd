#!/bin/bash

dir=$(readlink -f $(dirname $0))
out=$dir/artifacts

declare -A COPY_DIRS
COPY_DIRS[rpm]="RPMS/x86_64 RPMS/noarch SRPMS"
COPY_DIRS[deb]="DEBS/**"

declare -A POOLS
POOLS[rpm]="x86_64/Packages"
POOLS[deb]="pool"

# Make 'info' file with the artifacts details
make_info () {
BUILD_NUMBER='local_build'
[ -n "$GITHUB_RUN_NUMBER" ] && BUILD_NUMBER=$GITHUB_RUN_NUMBER
[ -n "$DRONE_BILD_NUMBER" ] && BUILD_NUMBER=$DRONE_BILD_NUMBER

SOURCE_BRANCH=`git rev-parse --abbrev-ref HEAD`
COMMIT_SHA=`git rev-parse --short HEAD`

mkdir -p $out/$dist_name
cat > $out/$dist_name/elastio-snap.info << INFO
Branch:     $SOURCE_BRANCH
Rev:        $COMMIT_SHA
Version:    `grep Version: $dir/../dist/elastio-snap.spec | awk '{print $NF}'`
Build:      $BUILD_NUMBER
INFO
}

if `which apt-get >/dev/null 2>&1` ; then
    dist_ver=$(grep VERSION_ID /etc/os-release | tr -cd '[0-9]')
    dist_name=$(grep '^ID=' /etc/os-release | cut -d'=' -f2)
    dist_name=${dist_name^}
    pkg_type="deb"
elif `which yum >/dev/null 2>&1` ; then
    dist_ver=$(cat /etc/system-release 2>/dev/null | tr -cd '[0-9.]' | cut -d'.' -f1)
    # Expected dist_name-s: Amazon CentOS Fedora
    dist_name=$(cat /etc/system-release 2>/dev/null | cut -d' ' -f1)
    pkg_type="rpm"
fi

if [ -z $dist_ver ] || [ -z $dist_name ]; then
    echo "Unknown Linux distro"
    exit 1
fi

[ "$dist_name" = "Debian" ] && make_info

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
