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
cmake_minimum_required (VERSION 3.11)

# Project
project(HelloService CXX)

set (CMAKE_CXX_STANDARD 11)

# external dependencies
# find_package(Boost 1.55 COMPONENTS system thread filesystem REQUIRED)
if (NOT vsomeip3_POPULATED)
  find_package(vsomeip3)
endif()
find_package(Threads REQUIRED)

function (matchVariables _prefix _varResult)
    get_cmake_property(_vars VARIABLES)
    string (REGEX MATCHALL "(^|;)${_prefix}[A-Za-z0-9_]*" _matchedVars "${_vars}")
    set (${_varResult} ${_matchedVars} PARENT_SCOPE)
endfunction()
# print vsomeip3 variables
matchVariables("vsomeip3_" matchedVars)
foreach (_var IN LISTS matchedVars)
    message("  -- ${_var}=${${_var}}")
endforeach()

# add_definitions(-DVSOMEIP_ENABLE_SIGNAL_HANDLING=1)

# HelloWorld Service
add_executable(hello_service
    hello_service.cc
    timer.cc
    hello_utils.cc
)
target_link_libraries(hello_service
    vsomeip3
    pthread
)
target_include_directories(hello_service
  PUBLIC
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE_HEADER # for byteorder.hpp
    ${vsomeip3_SOURCE_DIR}/implementation/utility/include
)

# add_subdirectory(vsomeip_adapter)

add_executable(hello_client
    hello_client.cc
    hello_utils.cc
)
target_include_directories(hello_client
  PUBLIC
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE_HEADER # for byteorder.hpp
    ${vsomeip3_SOURCE_DIR}/implementation/utility/include
)
target_link_libraries(hello_client
    vsomeip3
    pthread
)

install(TARGETS
    hello_service
    hello_client
    RUNTIME DESTINATION "${CMAKE_INSTALL_PREFIX}/bin"
)

###################################################################################################
