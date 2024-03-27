# syntax=docker/dockerfile:1
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

FROM --platform=$TARGETPLATFORM ubuntu:20.04 as builder

ARG TARGETPLATFORM  #=linux/amd64
ARG BUILDPLATFORM   #=linux/amd64

RUN echo "-- Running on $BUILDPLATFORM, Building for $TARGETPLATFORM"

ENV DEBIAN_FRONTEND="noninteractive"
RUN DEBIAN_FRONTEND=noninteractive apt-get update -y && \
    apt-get install -y git \
    cmake g++ build-essential

# install build deps
RUN DEBIAN_FRONTEND=noninteractive apt-get -y install --no-install-recommends \
    libboost-atomic-dev libboost-system-dev libboost-thread-dev libboost-filesystem-dev

COPY . /src

WORKDIR /build
RUN mkdir -p "/app/target-$TARGETPLATFORM"
RUN cmake \
    -DCMAKE_INSTALL_PREFIX=/app/target-$TARGETPLATFORM \
    -DCMAKE_BUILD_TYPE=Release \
    /src
RUN cmake --build . -j --target install/strip
RUN find /app/target-$TARGETPLATFORM 1>&2

FROM --platform=$TARGETPLATFORM ubuntu:20.04 as runtime
ARG TARGETPLATFORM
ARG BUILDPLATFORM

# NOTE: adjust boost version to match ubuntu dist (not using -dev packages to reduce target image size)
RUN DEBIAN_FRONTEND=noninteractive apt-get update -y && \
    apt-get install -y --no-install-recommends jq \
    libboost-atomic1.71.0  libboost-system1.71.0 libboost-thread1.71.0 libboost-filesystem1.71.0 && \
    rm -rf /var/lib/apt/lists/*

WORKDIR "/app/lib"
COPY --from=builder "/app/target-$TARGETPLATFORM/lib/*.so.3" "/app/lib/"

WORKDIR "/app/bin"
COPY --from=builder "/app/target-$TARGETPLATFORM/bin" "/app/bin/"
# include readme && license
COPY --from=builder "/src/LICENSE" "/app/"
COPY --from=builder "/src/README.md" "/app/"

# make sure vsomeip libs are in path
ENV LD_LIBRARY_PATH="/app/lib"

# Relevant for localtime_r()
ENV TZ=UTC

# expose configured SOME/IP Service Discovery port (in config/*.json)
EXPOSE 30490/UDP

ENTRYPOINT [ "/app/bin/docker-entrypoint.sh" ]
