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

export VSOMEIP_APPLICATION_NAME="hello_service"
export VSOMEIP_CONFIGURATION="$SCRIPT_DIR/config/hello_service.json"

### replace host ip to enable remote endpoint (udp) usecases
if [ -z "$CFG_UNICAST" ]; then
	CFG_UNICAST="$(hostname -I | cut -d ' ' -f 1)"
    echo "# unicast: $CFG_UNICAST"
fi
OLD_UNICAST=$(jq -r '.unicast' "$VSOMEIP_CONFIGURATION")
if [ "$OLD_UNICAST" != "$CFG_UNICAST" ]; then
    echo "### $VSOMEIP_CONFIGURATION: Replacing unicast [$OLD_UNICAST] with [$CFG_UNICAST]"
    jq --arg ip "$CFG_UNICAST" '.unicast=$ip' "$VSOMEIP_CONFIGURATION" > "$VSOMEIP_CONFIGURATION.tmp" && mv "$VSOMEIP_CONFIGURATION.tmp" "$VSOMEIP_CONFIGURATION"
fi

# get declared services
SERVICE_COUNT=$(jq -r '.services | length' "$VSOMEIP_CONFIGURATION")
if [ -n "$SERVICE_COUNT" ] && [ "$SERVICE_COUNT" -gt 1 ]; then
    UP_SERVICES=$(jq -r '[.services[].service] | join(",")' "$VSOMEIP_CONFIGURATION")
    echo "### $VSOMEIP_CONFIGURATION: UP Services: [$UP_SERVICES]"
    export UP_SERVICES
fi

# export LD_LIBRARY_PATH=/usr/local/lib
# export LD_LIBRARY_PATH="$BUILD_DIR/_deps/vsomeip3-build:$LD_LIBRARY_PATH"
if [ -d "$SCRIPT_DIR/../lib" ]; then
    export LD_LIBRARY_PATH="$SCRIPT_DIR/../lib:$LD_LIBRARY_PATH"
else
    echo "Missing $SCRIPT_DIR/../lib. Are you running from installed dir?"
fi

echo
echo "To Run Hello Service Example:"
echo "  . setup-service.sh"
echo "  DEBUG=0 ./hello_service --timers 1m:1,1s:1,1ms:1,10ms:1"
echo
