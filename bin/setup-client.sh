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

export VSOMEIP_APPLICATION_NAME="hello_client"
export VSOMEIP_CONFIGURATION="$SCRIPT_DIR/config/hello_client.json"

if [ ! -f "$VSOMEIP_CONFIGURATION" ]; then
    echo "Missing $VSOMEIP_CONFIGURATION. Are you running from installed dir?"
fi

### replace host ip to enable remote endpoint (udp) usecases
if [ -z "$CFG_UNICAST" ]; then
	CFG_UNICAST="$(hostname -I | cut -d ' ' -f 1)"
    echo "# Detected unicast: $CFG_UNICAST"
fi
OLD_UNICAST=$(jq -r '.unicast' "$VSOMEIP_CONFIGURATION")
if [ "$OLD_UNICAST" != "$CFG_UNICAST" ]; then
    echo "### $VSOMEIP_CONFIGURATION: Replacing unicast [$OLD_UNICAST] with [$CFG_UNICAST]"
    jq --arg ip "$CFG_UNICAST" '.unicast=$ip' "$VSOMEIP_CONFIGURATION" > "$VSOMEIP_CONFIGURATION.tmp" && mv "$VSOMEIP_CONFIGURATION.tmp" "$VSOMEIP_CONFIGURATION"
fi

if [ -d "$SCRIPT_DIR/../lib" ]; then
    export LD_LIBRARY_PATH="$SCRIPT_DIR/../lib:$LD_LIBRARY_PATH"
else
    echo "Missing $SCRIPT_DIR/../lib. Are you running from installed dir?"
fi

echo
echo "To Run Hello Service Client: "
echo "  . setup-client.sh"
echo "  DEBUG=0 ./hello_client World! --req 100 --sub"
echo
