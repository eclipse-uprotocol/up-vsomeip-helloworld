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
cmake_policy(SET CMP0095 NEW)
cmake_minimum_required(VERSION 3.11)

# Project
project(HelloWorldSomeIP CXX)

# set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE INTERNAL "If enabled, generates a compile_commands.json file")

set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")

###########################################
### vsomeip support as cmake dependency ###
###########################################

# Workaround for outdated cmake code in vsomeip handling Boost_VERSION
# include (FindBoost)
find_package(Boost 1.55 COMPONENTS system thread filesystem REQUIRED)
if(Boost_FOUND)
  MESSAGE(NOTICE "Boost_VERSION: ${Boost_VERSION}")
  if (NOT DEFINED Boost_VERSION_MACRO)
    string(REGEX MATCH "^([0-9]+)\\.([0-9]+)\\.([0-9]+)" BOOST_VERSION_MATCH ${Boost_VERSION})
    MATH(EXPR detected_boost_ver "${CMAKE_MATCH_1} * 100000 + ${CMAKE_MATCH_2} * 100 + ${CMAKE_MATCH_3}" )
    # set(Boost_VERSION_MACRO 107200)
    set(Boost_VERSION_MACRO ${detected_boost_ver})
    MESSAGE(STATUS "Boost_VERSION_MACRO set to: ${Boost_VERSION_MACRO}")
  endif()
else()
  MESSAGE(WARNING "Boost was not found!")
endif()

# build vsomeip from sources, no connan support
include(FetchContent)

# we want signal handling enabled for vsomeip...
# add_definitions(-DVSOMEIP_ENABLE_SIGNAL_HANDLING=1)

set(FETCHCONTENT_QUIET "OFF")
set (CMAKE_WARN_DEPRECATED "OFF")

set(VSOMEIP_TAG "3.4.10")
# set(VSOMEIP_TAG "3.1.20.3")

set(VSOMEIP_PATCH_PATH "${CMAKE_SOURCE_DIR}/patches/${VSOMEIP_TAG}")
MESSAGE(STATUS "FetchContent: Checking for patches in ${VSOMEIP_PATCH_PATH} ...")
file(GLOB_RECURSE VSOMEIP_PATCHES_LIST "${VSOMEIP_PATCH_PATH}/[0-9][0-9][0-9]-*.patch")
MESSAGE(STATUS "FetchContent: Applying patches: [${VSOMEIP_PATCHES_LIST}]")

FetchContent_Declare(
  vsomeip3
  GIT_REPOSITORY    https://github.com/COVESA/vsomeip.git
  GIT_TAG           ${VSOMEIP_TAG}
  PATCH_COMMAND     git apply --reject --ignore-whitespace --verbose --stat --apply
                       ${VSOMEIP_PATCHES_LIST} || true # don't fail if already patched...
  UPDATE_DISCONNECTED 1
  GIT_SHALLOW       ON
  GIT_PROGRESS      OFF
)
MESSAGE(STATUS "FetchContent: MakeAvailable(vsomeip3)...")
FetchContent_MakeAvailable(vsomeip3)

# strip binaries
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -s")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -s")

# add sources after vsomeip3 is populated
add_subdirectory(src)

###
### install scripts and configs
###
install(PROGRAMS
  ./bin/docker-entrypoint.sh
  ./bin/setup-service.sh
  ./bin/setup-client-proxy.sh
  ./bin/setup-client.sh
  DESTINATION "${CMAKE_INSTALL_PREFIX}/bin"
)

install(FILES
  ./config/hello_client-proxy.json
  ./config/hello_client.json
  ./config/hello_service.json
  DESTINATION "${CMAKE_INSTALL_PREFIX}/bin/config"
)
