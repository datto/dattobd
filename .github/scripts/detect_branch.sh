case $GITHUB_EVENT_NAME in
    pull_request) source_branch="${GITHUB_HEAD_REF#refs/heads/}" ;;
    push)         source_branch="${GITHUB_REF#refs/heads/}" ;;
    *)            source_branch=$(git rev-parse --abbrev-ref HEAD) ;;
esac
echo $source_branch
