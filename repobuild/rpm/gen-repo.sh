#!/bin/bash

set +x

die () {
    echo $@ >&2
    exit 1
}

dir=$(readlink -f $(dirname $0))
out=$dir/repo
dist_ver=`cat /etc/redhat-release | tr -cd [0-9.] | cut -d'.' -f1`
cent=$out/CentOS/$dist_ver/x86_64
pool=$cent/Packages
mirrors=$out/CentOS/mirrors
branch=`git rev-parse --abbrev-ref HEAD`
repo_link=https://repo.assur.io/assurio-snap

[ -z "$GPG_KEY_ID" ]   && die "Env var GPG_KEY_ID is not set"
[ -z "$GPG_PUB_KEY" ]  && die "Env var GPG_PUB_KEY is not set"
[ -z "$GPG_PRIV_KEY" ] && die "Env var GPG_PRIV_KEY is not set"

# Import gpg key
echo "$GPG_PRIV_KEY" | gpg2 --import --batch >/dev/null

# Check gpg key import result
gpg2 --list-secret-key $GPG_KEY_ID >/dev/null || \
     die "Failed to import gpg key"

# Cleanup and prepare dir structure
rm -rf $cent $mirrors/$dist_ver*
mkdir -p $cent/{Packages,repodata}
mkdir -p $mirrors

# Prepare 'Packages' dir. It's analog of the 'pool' dir for deb
cp -r $dir/../../pkgbuild/RPMS/x86_64/* $pool
cp -r $dir/../../pkgbuild/RPMS/noarch/* $pool
cp -r $dir/../../pkgbuild/SRPMS/*       $pool

# Prepare .rpmmacros
echo "%_gpg_name Assurio Software Package Developers <packages@assurio.com>" >> ~/.rpmmacros

#Sign packages
for f in `ls $pool/*.rpm`; do
    $dir/rpm-sign.exp $f || die "Failed to sign rpm"
done

# Generate repodata/repomd.xml
createrepo $cent || die "Failed to create repomd.xml"

# Sign repository (repomd.xml files)
gpg2 --default-key $GPG_KEY_ID --batch --yes --no-tty --armor --digest-algo SHA256 --detach-sign $cent/repodata/repomd.xml || \
    die "Failed to sign repomd.xml file."

# Generate 'mirrors'
mirrors_content="$repo_link/$branch/rpm/CentOS/\$releasever/\$basearch"
for kind in '' 'Client' 'Server' 'Workstation'; do
    echo $mirrors_content > $mirrors/$dist_ver$kind
done

# Show repo's tree to the log
tree -h $out
