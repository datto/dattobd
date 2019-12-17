#!/bin/bash

set +x

die () {
    echo $@ >&2
    exit 1
}

dir=$(readlink -f $(dirname $0))
out=$dir/repo
cent6=$out/CentOS/6
pool=$cent6/x86_64/Packages
branch=`git rev-parse --abbrev-ref HEAD`
bucket=ci.assur.io

[ -z "$GPG_KEY_ID" ]   && die "Env var GPG_KEY_ID is not set"
[ -z "$GPG_PUB_KEY" ]  && die "Env var GPG_PUB_KEY is not set"
[ -z "$GPG_PRIV_KEY" ] && die "Env var GPG_PRIV_KEY is not set"

# Import gpg key
echo "$GPG_PRIV_KEY" | gpg2 --import --batch >/dev/null

# Check gpg key import result
gpg2 --list-secret-key $GPG_KEY_ID >/dev/null || \
     die "Failed to import gpg key"

# Cleanup and prepare dir structure
rm -rf $out
mkdir -p $cent6/x86_64/{Packages,repodata}
mkdir -p $out/CentOS/mirrors

# Prepare 'Packages' dir. It's analog of the 'pool' dir for deb
cp -r $dir/../../pkgbuild/RPMS/x86_64/* $pool
cp -r $dir/../../pkgbuild/RPMS/noarch/* $pool
cp -r $dir/../../pkgbuild/SRPMS/*       $pool

# Prepare .rpmmacros
echo "%_gpg_name Assurio Software Package Developers <packages@assurio.com>" >> ~/.rpmmacros

#Sign packages
find $pool -type f -name "*.rpm" -exec $dir/rpm-sign.exp {} \;

# Generate repodata/repomd.xml
createrepo $cent6/x86_64

# Sign repository (repomd.xml files)
gpg2 --default-key $GPG_KEY_ID --batch --yes --no-tty --armor --digest-algo SHA256 --detach-sign $cent6/x86_64/repodata/repomd.xml

# Generate 'mirrors'
mirrors_content="http://$bucket.s3.amazonaws.com/repo/assurio-snap/$branch/rpm/CentOS/\$releasever/\$basearch"
for t in '' 'Client' 'Server' 'Workstation'; do
	echo $mirrors_content > $out/CentOS/mirrors/6$t
done

# Show repo's tree to the log
tree -h $out
