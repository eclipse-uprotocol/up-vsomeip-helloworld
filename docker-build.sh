#!/bin/bash
#********************************************************************************
# Copyright (c) 2024 Contributors to the Eclipse Foundation
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# SPDX-License-Identifier: Apache-2.0
#*******************************************************************************
# shellcheck disable=SC2086,SC2181

export COL_NONE="\033[0m"
export COL_RED="\033[0;31m"
export COL_BLUE="\033[0;34m"
export COL_YELLOW="\033[0;33m"
export COL_WHITE="\033[0;37m"
export COL_WHITE_BOLD="\033[1;37m"
export COL_GREEN="\033[0;32m"

LOCAL=0
VERBOSE=0
TARGET="amd64"

print_usage() {
	echo "USAGE: $0 [OPTIONS] TARGETS"
	echo
	echo "Standalone build helper for Hello Service/Client container."
	echo
	echo "OPTIONS:"
	echo "  -l, --local      local docker import (does not push to ttl.sh)"
	echo "  -v, --verbose    enable plain docker output and disable cache"
	echo "      --help       show help"
	echo
	echo "TARGETS:"
	echo "  amd64|arm64    Target arch to build for (default: amd64)"
	echo
}

print_example() {
	local image="$1"
	echo ""
	echo "- To run Hello Service:"
	echo "  $ docker run -e DEBUG=0 -it --rm $image --timers 1s:1,1m:1,10ms:1,1ms:1"
	echo ""
	echo "- To run Hello Client:"
	echo "  $ docker run -e DEBUG=0 -it --rm $image --client --sub --req 100 DockerClient!"
	echo ""
	echo "# If you need to connect to external SOME/IP add --network=host after 'docker run'"
	echo ""
}

while [ $# -gt 0 ]; do
	if [ "$1" = "--local" ] || [ "$1" = "-l" ]; then
		LOCAL=1
	elif [ "$1" = "--verbose" ] || [ "$1" = "-v" ]; then
		VERBOSE=1
	elif [ "$1" = "--help" ]; then
		print_usage
		exit 0
	else
		TARGET="$1"
	fi
	shift
done

if [ "$TARGET" != "arm64" ] && [ "$TARGET" != "amd64" ]; then
    echo "Invalid target: $TARGET"
	print_usage
	exit 1
fi

if [ "$VERBOSE" = "1" ]; then
	DOCKER_ARGS="--no-cache --progress=plain $DOCKER_ARGS"
fi

if [ "$LOCAL" = "1" ]; then
    DOCKER_ARGS="--load $DOCKER_ARGS"
fi

DOCKER_BUILDKIT=1 docker buildx build $DOCKER_ARGS --platform linux/$TARGET -f Dockerfile -t $TARGET/hello_service . "$@"
[ $? -eq 0 ] || exit 1

if [ "$LOCAL" = "0" ]; then
    DOCKER_IMAGE="ttl.sh/hello_service-$TARGET-$(uuidgen):24h"
    docker tag "$TARGET/hello_service" "$DOCKER_IMAGE"
    docker push "$DOCKER_IMAGE"
    if [ $? -eq 0 ]; then
        echo -e "$COL_WHITE_BOLD"
        echo -e "### Image pushed to: ${COL_YELLOW}$DOCKER_IMAGE"
        echo -e "$COL_NONE"
    fi
else
	DOCKER_IMAGE="$TARGET/hello_service"
fi

echo "Built Docker image:"
docker image ls | grep hello_service

print_example "$DOCKER_IMAGE"
