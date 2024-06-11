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
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

####################################################################################################################
# Usage $0 [args]               Launch Hello Service with optional arguments.
#          [--client] [args]    Launch Hello Client instead of Hello Service, with remaining arguments.
#          [--gen [count]]      Regenerate Service config with optional service count, applying UP_* env vars.
#
####################################################################################################################

update_up_config() {
    local config_file="$1"
    local old_config
    old_config=$(jq '.' "$config_file")
    if [ -z "$old_config" ]; then
        echo "### $config_file: Error parsing JSON!" 1>&2
        return 1
    fi
    local json_contents="$old_config"

    # optionally update unicast address and service discovery settings
    if [ -n "$UP_UNICAST" ]; then
        json_contents=$(echo "$json_contents" | jq --arg ip "$UP_UNICAST" '.unicast=$ip')
    fi
    if [ -n "$UP_SD_MULTICAST" ]; then
        json_contents=$(echo "$json_contents" | jq --arg ip "$UP_SD_MULTICAST" '."service-discovery".multicast=$ip')
    fi
    if [ -n "$UP_SD_PORT" ]; then
        json_contents=$(echo "$json_contents" | jq --arg port "$UP_SD_PORT" '."service-discovery".port=$port')
    fi
    if [ -n "$UP_SD_TTL" ]; then
        json_contents=$(echo "$json_contents" | jq --arg ttl "$UP_SD_TTL" '."service-discovery".ttl=$ttl')
    fi

    # only update if something changed
    if [ "$old_config" != "$json_contents" ]; then
        echo "$json_contents" > "$config_file.tmp" && mv "$config_file.tmp" "$config_file"
        echo "### $config_file: Updated."
        cat "$config_file"
        echo
    fi
    return 0
}

USE_CLIENT=0
# allow easy switching between client/service
if [ "$1" == "--client" ]; then
    shift
    USE_CLIENT=1
    echo "# Configuring as Hello Client, args: $*"
    if [ -z "$VSOMEIP_APPLICATION_NAME" ]; then
        export VSOMEIP_APPLICATION_NAME="hello_client"
    fi
    if [ -z "$VSOMEIP_CONFIGURATION" ]; then
        if [ -f "$SCRIPT_DIR/config/hello_client.json" ]; then
            export VSOMEIP_CONFIGURATION="$SCRIPT_DIR/config/hello_client.json"
        fi
    fi
fi

if [ -z "$VSOMEIP_CONFIGURATION" ]; then
    if [ -f "$SCRIPT_DIR/config/hello_service.json" ]; then
        VSOMEIP_CONFIGURATION="$SCRIPT_DIR/config/hello_service.json"
    fi
    export VSOMEIP_CONFIGURATION
fi
[ -z "$VSOMEIP_APPLICATION_NAME" ] && export VSOMEIP_APPLICATION_NAME="hello_service"

# set default unicast address if not already provided
if [ -z "$UP_UNICAST" ]; then
	UP_UNICAST="$(hostname -I | cut -d ' ' -f 1)"
    echo "# Detected unicast: $UP_UNICAST"
fi

# default debug levels
[ -z "$DEBUG" ] && export DEBUG=1 ### INFO

# Allow regenerating Service config with optional number of services and custom id values from UP_* env. vars
if [ $USE_CLIENT -ne 1 ] && [ "$1" == "--gen" ]; then
    shift
    GEN_SERVICE_COUNT="$1"
    shift
    [ -z "$GEN_SERVICE_COUNT" ] && GEN_SERVICE_COUNT=1

    echo "### Regenerating Service config: $VSOMEIP_CONFIGURATION"
    echo '--- ENV ---'
    env | grep -e "UP_\|VSOMEIP_" | sort
    echo '---'
    echo
    ./gen-services.sh "$VSOMEIP_CONFIGURATION" "$GEN_SERVICE_COUNT" "$VSOMEIP_CONFIGURATION"

    # export list of used (alternative) service IDs
    UP_SERVICES=$(jq -r '[.services[].service] | join(",")' "$VSOMEIP_CONFIGURATION")
    echo "### UP Services: [$UP_SERVICES]"
    export UP_SERVICES
    echo
fi

echo

if [ -z "$VSOMEIP_APPLICATION_NAME" ]; then
    echo "WARNING! VSOMEIP_APPLICATION_NAME not set in environment!"
fi
if [ ! -f "$VSOMEIP_CONFIGURATION" ]; then
    echo "WARNING! Can't find VSOMEIP_CONFIGURATION: $VSOMEIP_CONFIGURATION"
else
    echo "****************************"
    echo "SOME/IP config: $VSOMEIP_CONFIGURATION"

    # replace common UP_UNICAST, UP_SD_* variables 
    if ! update_up_config "$VSOMEIP_CONFIGURATION"; then
        exit 1
    fi

    ### Sanity checks for application name
    CONFIG_APP=$(jq -r  '.applications[0].name' "$VSOMEIP_CONFIGURATION")
    ROUTING_APP=$(jq -r '.routing' "$VSOMEIP_CONFIGURATION")
    UNICAST_APP=$(jq -r '.unicast' "$VSOMEIP_CONFIGURATION")
    echo "json config: { app_name: $CONFIG_APP, routinng: $ROUTING_APP, unicast: $UNICAST_APP }"
    echo "****************************"
    echo ""

    if [ "$CONFIG_APP" != "$VSOMEIP_APPLICATION_NAME" ]; then
        echo "WARNING! $VSOMEIP_CONFIGURATION has application name: $CONFIG_APP, but VSOMEIP_APPLICATION_NAME is: $VSOMEIP_APPLICATION_NAME"
    fi
fi

# if running from install, export LD_LIBRARY_PATH to vsomeip libs.
[ -d "$SCRIPT_DIR/../lib" ] && export LD_LIBRARY_PATH="$SCRIPT_DIR/../lib:$LD_LIBRARY_PATH"

echo "# ./$VSOMEIP_APPLICATION_NAME $*"
"./$VSOMEIP_APPLICATION_NAME" "$@"
