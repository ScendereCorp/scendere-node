#!/bin/bash
set -e

scripts="$PWD/ci"
if [[ "$GITHUB_WORKFLOW" = "Develop" ]]; then
    "$scripts"/custom-timeout.sh 30 docker push "ghcr.io/${GITHUB_REPOSITORY}/scendere-env:base"
    "$scripts"/custom-timeout.sh 30 docker push "ghcr.io/${GITHUB_REPOSITORY}/scendere-env:gcc"
    "$scripts"/custom-timeout.sh 30 docker push "ghcr.io/${GITHUB_REPOSITORY}/scendere-env:clang-6"
else
    tags=$(docker images --format '{{.Repository}}:{{.Tag }}' | grep "ghcr.io" | grep -vE "env|none")
    for a in $tags; do
        "$scripts"/custom-timeout.sh 30 docker push "$a"
    done
fi
