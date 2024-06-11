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
# shellcheck disable=SC2034
# shellcheck disable=SC2155

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# default values
USE_TCP=0
USE_UDP=0
SERVICE_COUNT=1

print_usage() {
    echo
    echo "USAGE: $0 INPUT SERVICE_COUNT [OUTPUT]"
    echo
    echo "Helper for regeneration of Hello Service json config. Allows adding multiple services, changing default service id, instance id, ports, etc."
    echo
    echo "ARGS:"
    echo "  INPUT           vsomeip config json file"
    echo "  SERVICE_COUNT   number of services to generate [1..]"
    echo "  OUTPUT          vsomeip config output file. If not set, dumps to stdout."
    echo
    echo "OPTIONS:"
    echo "  --help           show help"
    echo
    echo "ENVIRONMENT:"
    echo "  UP_SERVICE       Update HelloService service_id in template."
    echo "  UP_INSTANCE      Update HelloService instance_id in template."
    echo "  UP_TCP           Update HelloService TCP port in template."
    echo "  UP_UDP           Update HelloService UDP port in template."
    echo "  UP_UNICAST       Update Host IP in config."
    echo "  UP_SD_MULTICAST  Update Service Discovery multicast address in config."
    echo "  UP_SD_PORT       Update Service Discovery port in config."
    echo "  UP_SD_TTL        Update Service Discovery TTL in config."

    echo
}

build_service() {
    local template="$1"
    local id="$(printf '0x%x' "$2")"
    echo "$template" | jq -c --arg service_id "$id" '.service=$service_id'
}

if [ "$1" = "--help" ]; then
    print_usage 1>&2
    exit 0
fi

# args
VSOMEIP_CONFIGURATION="$1"
SERVICE_COUNT="$2"
OUTPUT_FILE="$3"

[ -z "$SERVICE_COUNT" ] && SERVICE_COUNT=1
[ -n "$UP_TCP" ] && USE_TCP=$((UP_TCP))
[ -n "$UP_UDP" ] && USE_UDP=$((UP_UDP))

if [ -z "$VSOMEIP_CONFIGURATION" ] || ! [ -f "$VSOMEIP_CONFIGURATION" ]; then
    echo "Invalid input config: $VSOMEIP_CONFIGURATION" 1>&2
    print_usage 1>&2
    exit 1
fi

if [ "$SERVICE_COUNT" -lt 1 ]; then
    echo "### Invalid SERVICE_COUNT: $SERVICE_COUNT" 1>&2
    print_usage 1>&2
    exit 1
fi

SERVICES=$(jq -r  '.services | length' "$VSOMEIP_CONFIGURATION")
if [ -z "$SERVICES" ]; then
    echo "### $VSOMEIP_CONFIGURATION: Error parsing '.services'" 1>&2
    exit 1
fi
if [ "$SERVICES" -gt 1 ]; then
    echo "### $VSOMEIP_CONFIGURATION: Found $SERVICES services, only the 1st will be used as template!" 1>&2
fi
if [ "$SERVICES" -le 0 ]; then
    echo "### $VSOMEIP_CONFIGURATION: Does not contain a service template!" 1>&2
    exit 1
fi 

# read service[] as template
SERVICE_TEMPLATE=$(jq -c '.services[0]' "$VSOMEIP_CONFIGURATION")

# update service and instance in template if env var were set
if [ -n "$UP_SERVICE" ]; then
    HEX_SERVICE=$(printf '0x%04x' "$((UP_SERVICE))")
    SERVICE_TEMPLATE=$(echo "$SERVICE_TEMPLATE" | jq -c --arg service "$HEX_SERVICE" '.service=$service')
fi
if [ -n "$UP_INSTANCE" ]; then
    HEX_INSTANCE=$(printf '0x%04x' "$((UP_INSTANCE))")
    SERVICE_TEMPLATE=$(echo "$SERVICE_TEMPLATE" | jq -c --arg instance "$HEX_INSTANCE" '.instance=$instance')
fi
# handle tcp/udp ports (0=don't use, both 0=don't replace)
if [ "$USE_TCP" -gt 0 ] && [ "$USE_UDP" -gt 0 ]; then
    SERVICE_TEMPLATE=$(echo "$SERVICE_TEMPLATE" | jq -c --arg tcp_port "$UP_TCP" --arg udp_port "$UP_UDP" '.unreliable=$udp_port | .reliable.port=$tcp_port')
elif [ "$USE_TCP" -gt 0 ]; then
    SERVICE_TEMPLATE=$(echo "$SERVICE_TEMPLATE" | jq -c --arg port "$UP_TCP" 'del(.unreliable) | .reliable.port=$port')
elif [ "$USE_UDP" -gt 0 ]; then
    SERVICE_TEMPLATE=$(echo "$SERVICE_TEMPLATE" | jq -c --arg port "$UP_UDP" 'del(.reliable) | .unreliable=$port')
fi

SERVICE_STR="$(echo "$SERVICE_TEMPLATE" | jq -r '.service')"
INSTANCE_STR="$(echo "$SERVICE_TEMPLATE" | jq -r '.instance')"
UDP_STR="$(echo "$SERVICE_TEMPLATE" | jq -r '.unreliable // "n/a"')"
TCP_STR="$(echo "$SERVICE_TEMPLATE" | jq -r '.reliable.port // "n/a"')"

SERVICE_ID=$((SERVICE_STR)) # takes care of hex (0x) and decimal values

id="$SERVICE_ID"
SERVICES_MAX=$((SERVICE_ID+SERVICE_COUNT))

echo "# SERVICE_ID       : $( printf '[0x%04X .. 0x%04X]' $SERVICE_ID $((SERVICES_MAX - 1)) )"
echo "# INSTANCE_ID      : $( printf '0x%04X' $((INSTANCE_STR)) )"
[ "$VERBOSE" = "1" ] && echo "# UNICAST          : $UP_UNICAST"
[ "$VERBOSE" = "1" ] && echo "# UDP_PORT         : $UDP_STR"
[ "$VERBOSE" = "1" ] && echo "# TCP_PORT         : $TCP_STR"
[ "$VERBOSE" = "1" ] && echo "# SERVICE_TEMPLATE : $SERVICE_TEMPLATE"
[ "$VERBOSE" = "1" ] && echo

SERVICES_ARRAY="[]"
[ "$VERBOSE" = "1" ] && echo "# Generating services..."
while [ $id -lt $SERVICES_MAX ]; do
    [ "$VERBOSE" = "1" ] && printf "  - Service:0x%04X\r" "$id"
    ENTRY=$(build_service "$SERVICE_TEMPLATE" "$id")
    SERVICES_ARRAY=$(echo "$SERVICES_ARRAY" | jq -c --argjson service "$ENTRY" '. += [$service]')
    ((id++))
done
[ "$VERBOSE" = "1" ] && printf "\r                         "
[ "$VERBOSE" = "1" ] && echo

# replace all services using 1st one as template, incrementing each service id
JSON_CONTENTS=$(jq --argjson services "$SERVICES_ARRAY" '.services=$services' "$VSOMEIP_CONFIGURATION") # > "$VSOMEIP_CONFIGURATION.tmp" && mv "$VSOMEIP_CONFIGURATION.tmp" "$VSOMEIP_CONFIGURATION"

# optionally update unicast address
if [ -n "$UP_UNICAST" ]; then
    JSON_CONTENTS=$(echo "$JSON_CONTENTS" | jq --arg ip "$UP_UNICAST" '.unicast=$ip')
fi
if [ -n "$UP_SD_MULTICAST" ]; then
    JSON_CONTENTS=$(echo "$JSON_CONTENTS" | jq --arg ip "$UP_SD_MULTICAST" '."service-discovery".multicast=$ip')
fi
if [ -n "$UP_SD_PORT" ]; then
    JSON_CONTENTS=$(echo "$JSON_CONTENTS" | jq --arg port "$UP_SD_PORT" '."service-discovery".port=$port')
fi
if [ -n "$UP_SD_TTL" ]; then
    JSON_CONTENTS=$(echo "$JSON_CONTENTS" | jq --arg ttl "$UP_SD_TTL" '."service-discovery".ttl=$ttl')
fi

if [ "$VERBOSE" = "1" ] || [ -z "$OUTPUT_FILE" ]; then
    echo "# Generated config:"
    echo "$JSON_CONTENTS" | jq '.'
    echo
fi

# safely write to output file
if [ -n "$OUTPUT_FILE" ]; then
    echo "### Writing to $OUTPUT_FILE"
    if [ "$OUTPUT_FILE" = "$VSOMEIP_CONFIGURATION" ]; then
        echo "$JSON_CONTENTS" > "$OUTPUT_FILE.tmp" && mv "$OUTPUT_FILE.tmp" "$OUTPUT_FILE"
    else
        echo "$JSON_CONTENTS" > "$OUTPUT_FILE"
    fi
fi

if [ "$VERBOSE" = "1" ]; then
    SERVICE_IDS=$(echo "$JSON_CONTENTS" | jq -r '[.services[].service] | join(",")')
    echo
    echo "### Generated Service IDs: $SERVICE_IDS"
fi