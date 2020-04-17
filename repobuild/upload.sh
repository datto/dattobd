#!/bin/bash

me=$(basename $0)
dir=$(readlink -f $(dirname $0))

usage()
{
    echo "This script uploads files to an s3 bucket."
    echo "Usage example: $me -s /path/to/source/files/* -b my-s3-bucket -t /path/in/the/bucket [-e ./**/exclude_files] [-n '*no-cache-files*'] [-t my_tag=some_value] -acl-public | -h"
    echo
    echo "  -s | --source          : Source loacation of the packages, like 'linux/repo/artifacts/deb/'. The relative path from the current directory."
    echo "  -b | --bucket          : Target bucket name."
    echo "  -t | --target          : Target directory of the packages in the s3 bucket, like '/repo/linux/\${DRONE_SOURCE_BRANCH}/deb'."
    echo "  -m | --mask            : Optional. Single mask as '-name' param for the 'find' command to filter file(s) in the source location."
    echo "  -e | --exclude         : Optional. A mask or list of comma-separated masks to exclude files from the '--source' path,"
    echo "                           like '*GPG-KEY', to not upload them."
    echo "  -n | --no-cache        : Optioanl. Some mask or list of comma-separated masks to distinct files from the source files,"
    echo "                           wich will be uploaded with the header 'Cache-Control: max-age=0'."
    echo "                           It could be something like '*Release*;*repo*' to exclude Release, InRelease, repo package etc."
    echo "                           These files aren't cached by Cloudfront."
    echo "  -t | --tag             : Optional. Add an s3 tag to all uploaded files. It looks like tag_name:subname=tag_value. DO NOT USE SPACES!!!"
    echo "  -p | --prefix-strip    : Optional. A part of the source path to strip when copying files to the target."
    echo "  -a | --acl-public      : Optional, without parameter. Use s3 ACL public-read for uploaded files. Private is default, if this arg is not specified."
    echo "  -o | --delete-outdated : Optional, without parameter. Remove obsolete files, present in the target location before the upload and not rewritten by new files."
    echo "  -h | --help            : Show this usage help."
}

# 1st arg - path to prettify
# 2nd arg - yes - remove leading slash, if present
# 3rd arg - yes - remove trailing slash, if present
prettify_path()
{
    local ret=`echo "${1}" | sed 's#//*#/#g'`

    if [ $ret != '/' ] && [ $ret != './' ]; then
        [[ "$2" == "yes" ]] && ret=`echo "${ret#/}"`
        [[ "$3" == "yes" ]] && ret=`echo "${ret%/}"`
    fi
    echo $ret
}

[ "$1" == "" ] && echo "Script need some arguments!" && usage && exit 1

while [ "$1" != "" ]; do
    case $1 in
        -s | --source)          shift && src=$(prettify_path $1 no yes) ;;
        -a | --acl-public)      acl="--acl public-read" ;;
        -b | --bucket)          shift && bucket="$1" ;;
        -t | --target)          shift && target=$(prettify_path $1 yes yes) ;;
        -m | --mask)            shift && mask="$1" ;;
        -e | --exclude)         shift && exclude="$1" ;;
        -n | --no-cache)        shift && no_cache="$1" ;;
        -t | --tag)             shift && tag="$1" ;;
        -p | --prefix-strip)    shift && strip_prefix=$(prettify_path $1 yes yes) ;;
        -o | --delete-outdated) del_olds=1 ;;
        -h | --help)            usage && exit ;;
        *)                      echo "Wrong arguments!"
                                echo
                                usage && exit 15 ;;
    esac
    shift
done

if [ -z "$src" ]; then
    echo "Missing '-s' arg which is source loacation of the packages."
    usage
    exit 2
fi

if [ ! -d $src ] && [ ! -f $src ] ; then
    echo "The source file or directory $src doesn't exist."
    exit 3
fi

if [ -z "$bucket" ]; then
    echo "Missing '-b' arg which is bucket name."
    usage
    exit 4
fi

if [ -z "$target" ]; then
    echo "Missing '-t' arg which is target directory of the packages in the s3 bucket."
    usage
    exit 5
fi

if [ ! -z "$exclude" ]; then
    for excl in ${exclude//,/ }; do
        exclusions="$exclusions ! -name $excl "
    done
fi

[ ! -z "$mask" ] && mask="-name $mask"
[ -z "$acl" ] && acl="--acl private"

files=($(find $src $mask $exclusions -type f))

if [ ! -z "$del_olds" ]; then
    old_files=($(aws s3 ls --recursive s3://$bucket/$target/ | tr -s ' ' | awk -F"$target/" '{print $2}'))
fi

if [ ! -z "$tag" ]; then
    tag="--tagging $tag"
fi

if [ ! -z "$no_cache" ]; then
    for f in ${no_cache//,/ }; do
        f_nc=($(find $src -name $f -type f))
        [ $f_nc ] && files_no_cache+=(${f_nc[@]})
    done
fi

for file in ${files[@]}; do

    dest_file=${file#"$src/"}
    [ ! -z $strip_prefix ] && dest_file=${dest_file#"$strip_prefix/"}

    nc=
    if [[ "${files_no_cache[@]}" =~ "$file" ]]; then
        nc="--cache-control max-age=0"
    fi

    out=$target/$dest_file
    if [ $target == '/' ] || [ $target == './' ]; then
        out=$dest_file
    fi

    set -x
    aws s3api put-object $acl $nc $tag --body $file --bucket $bucket --key $out || exit 7
    { set +x; } 2>/dev/null
    old_files=("${old_files[*]/$dest_file}")
done

if [ ! -z "$del_olds" ]; then
    echo "Removing obsolete files, present in the s3://$bucket/$target before upload."
    for file in ${old_files[@]}; do
        aws s3 rm s3://$bucket/$target/$file || exit 6
    done
fi
