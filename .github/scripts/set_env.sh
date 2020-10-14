set -x

dist_name=$(echo $DISTRO | grep -o -E '[a-z]+')
dist_name=$(d=${dist_name^} && echo ${d//os/OS})
dist_ver=$(echo $DISTRO | grep -o -E '[0-9]+')
case $dist_name in
    Debian|Ubuntu) pkg_type=deb ;;
    *)             pkg_type=rpm ;;
esac

runner_num=$(echo $GITHUB_WORKSPACE | grep -o -E '[0-9]+' | head -1)
source_branch=$(.github/scripts/detect_branch.sh)

echo "SOURCE_BRANCH=$source_branch"                     >> $GITHUB_ENV
echo "PKG_TYPE=$pkg_type"                               >> $GITHUB_ENV
echo "DIST_NAME=$dist_name"                             >> $GITHUB_ENV
echo "DIST_VER=$dist_ver"                               >> $GITHUB_ENV
echo "RUNNER_NUM=$runner_num"                           >> $GITHUB_ENV
echo "BOX_DIR=.github/buildbox"                         >> $GITHUB_ENV
echo "BOX_NAME=$DISTRO-$ARCH-build"                     >> $GITHUB_ENV
echo "INSTANCE_NAME=$DISTRO-$ARCH-build-$runner_num"    >> $GITHUB_ENV
