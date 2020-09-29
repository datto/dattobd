SOURCE_BRANCH="$(.github/scripts/detect_branch.sh)"
echo "Trigger repo build for the branch $SOURCE_BRANCH"
where_from="from"
[ "$GITHUB_EVENT_NAME" == "push" ] && where_from="to"
set -x
curl -XPOST -u "$REPO_HOOK_TOKEN" \
  -H "Accept: application/vnd.github.everest-preview+json" \
  -H "Content-Type: application/json" https://api.github.com/repos/elastio/packaging/dispatches \
  --data '{ "event_type": "elastio-snap #'$GITHUB_RUN_NUMBER': '$GITHUB_EVENT_NAME' '$where_from' '$SOURCE_BRANCH'", "client_payload": { "branch": "'$SOURCE_BRANCH'" } }'
