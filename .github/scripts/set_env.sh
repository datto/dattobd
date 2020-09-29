set -x
echo ::set-env name=SOURCE_BRANCH::$(.github/scripts/detect_branch.sh)

dist_name=$(echo $DISTRO | grep -o -E '[a-z]+')
dist_name=$(d=${dist_name^} && echo ${d//os/OS})
dist_ver=$(echo $DISTRO | grep -o -E '[0-9]+')
case $dist_name in
  Debian|Ubuntu) pkg_type=deb ;;
  *)             pkg_type=rpm ;;
esac
runner_num=$(echo $GITHUB_WORKSPACE | grep -o -E '[0-9]+' | head -1)
echo ::set-env name=PKG_TYPE::$pkg_type
echo ::set-env name=DIST_NAME::$dist_name
echo ::set-env name=DIST_VER::$dist_ver
echo ::set-env name=RUNNER_NUM::$runner_num
echo ::set-env name=BOX_DIR::".github/buildbox"
echo ::set-env name=BOX_NAME::$(echo $DISTRO-$ARCH-build)
echo ::set-env name=INSTANCE_NAME::$(echo $DISTRO-$ARCH-build-$runner_num)
