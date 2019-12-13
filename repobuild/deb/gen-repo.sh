#!/bin/bash

set +x

die () {
    echo $@ >&2
    exit 1
}

dir=$(readlink -f $(dirname $0))
out=$dir/repo
pool=$out/pool
conf=$dir/gen-repo.conf
suite=unstable #stable testing

[ -z "$GPG_KEY_ID" ]   && die "Env var GPG_KEY_ID is not set"
[ -z "$GPG_PUB_KEY" ]  && die "Env var GPG_PUB_KEY is not set"
[ -z "$GPG_PRIV_KEY" ] && die "Env var GPG_PRIV_KEY is not set"

# Import gpg key
echo "$GPG_PRIV_KEY" | gpg2 --import >/dev/null
echo "$GPG_PRIV_KEY" | gpg  --import >/dev/null

# Check gpg key import result
gpg2 --list-secret-key $GPG_KEY_ID >/dev/null && \
gpg  --list-secret-key $GPG_KEY_ID >/dev/null || \
     die "Failed to import gpg key"

# Cleanup and prepare dir structure
rm -rf $out
mkdir -p $pool
mkdir -p $dir/repo/dists/$suite/main/binary-{amd64,all}

# Prepare 'pool'
cp -r $dir/../../pkgbuild/DEBS/{amd64,all}/* $pool/

# Sign packages
dpkg-sig -k $GPG_KEY_ID --sign builder $pool/*.deb || die "Failed to sign deb packages"

# Generate 'Packages' and 'Contents' files
cd $out
sed -i.bak "s|stable|$suite|g" $conf
apt-ftparchive generate $conf || die "Failed to generate 'Packages' and 'Contents' files"

# Generate 'Release' file
apt-ftparchive -c $conf release dists/$suite > dists/$suite/Release || die "Failed to generate 'Release' file"
mv $conf.bak $conf

# Sign Release file and create InRelease, and Release.gpg files
gpg2 --default-key $GPG_KEY_ID --batch --yes --no-tty --armor --digest-algo SHA256 --clearsign -o dists/$suite/InRelease   dists/$suite/Release && \
gpg2 --default-key $GPG_KEY_ID --batch --yes --no-tty --armor --digest-algo SHA256 -abs        -o dists/$suite/Release.gpg dists/$suite/Release || \
     die "Failed to sign Release file"

# Show repo's tree to the log
tree -h .

# Put public gpg key to the root of the repo dir
echo "$GPG_PUB_KEY" > $out/ASSURIO-PKGS-GPG-KEY

# Make 'info' file with the repo details
[ -z "$DRONE_BUILD_NUMBER" ] && DRONE_BUILD_NUMBER='local_build'
cat > $out/info << INFO
Branch:		`git rev-parse --abbrev-ref HEAD`
Rev:		`git rev-parse --short HEAD`
Version:	`grep Version: $dir/../../dist/assurio-snap.spec | awk '{print $NF}'`
Build:		$DRONE_BUILD_NUMBER
INFO
