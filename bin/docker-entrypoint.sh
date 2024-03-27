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

# allow easy switching between client/service
if [ "$1" == "--client" ]; then
    shift
    echo "# Configuring as Hello Client: $*"
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

if [ -z "$CFG_UNICAST" ]; then
	CFG_UNICAST="$(hostname -I | cut -d ' ' -f 1)"
fi
echo "# Using unicast: $CFG_UNICAST"


# default debug levels
[ -z "$DEBUG" ] && export DEBUG=1 ### INFO

echo

if [ -z "$VSOMEIP_APPLICATION_NAME" ]; then
    echo "WARNING! VSOMEIP_APPLICATION_NAME not set in environment!"
fi
if [ ! -f "$VSOMEIP_CONFIGURATION" ]; then
    echo "WARNING! Can't find VSOMEIP_CONFIGURATION: $VSOMEIP_CONFIGURATION"
else
    echo "****************************"
    echo "SOME/IP config: $VSOMEIP_CONFIGURATION"
    ### Replace unicast address with Hostname -I (1st record)
    if grep -q "unicast" "$VSOMEIP_CONFIGURATION"; then
        echo "### Replacing uinicast: $CFG_UNICAST in $VSOMEIP_CONFIGURATION"
        jq --arg ip "$CFG_UNICAST" '.unicast=$ip' "$VSOMEIP_CONFIGURATION" > "$VSOMEIP_CONFIGURATION.tmp" && mv "$VSOMEIP_CONFIGURATION.tmp" "$VSOMEIP_CONFIGURATION"
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
