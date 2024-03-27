# up-vsomeip-helloworld
- [Overview](#overview)
  - [Building Hello Service](#building-hello-service)
    - [Prerequisites](#prerequisites)
    - [Build Docker images](#build-docker-images)
    - [Building with cmake](#building-with-cmake)
  - [Running Hello examples](#running-hello-examples)
    - [Running Hello Service/Client in Docker network](#running-hello-serviceclient-in-docker-network)
    - [Running standalone examples](#running-standalone-examples)
      - [Running Hello Service](#running-hello-service)
      - [Running Hello client](#running-hello-client)


# up-vsomeip-helloworld

COVESA/uServices HelloWorld example for mechatronics hello world uService and client using vsomeip.

This project provides **SOME/IP** Hello Service and Client, using a wire format suitable for
[HelloWorld uservice](https://github.com/COVESA/uservices/blob/main/src/main/proto/example/hello_world/v1/hello_world_service.proto).

Hello Service is implemented using [vsomeip](https://github.com/COVESA/vsomeip) and provides:
  - `SayHello` request/response method, that echoes back: `"Hello " + request` string.

  - Timer events, compatible with `TimeOfDay` proto. Each timer sends SOME/IP event with specified period.

    **NOTE:** In addition to to `hello_world_service` definitions, `1ms` and `10ms` timers are supported.

  - RPC call and event summary is printed upon termination.

Hello examples may be used to simulate **AUTOSAR** SOME/IP application or compare against another SOME/IP library.


## Building Hello Service

Hello service [CMakeLists.txt](./CMakeLists.txt) fetches specified `vsomeip` tag and patches it (as some problems were found).

E.g. to work with **AUTOSAR** SOME/IP apps, the following [patch](./patches/3.1.20.3/002-vsomeip-default-major-change.patch) may be needed.

**NOTE:** `vsomeip v3.1.20.3` is the old `stable` version, being changed a lot recently, but a recent `3.4.10` version is also supported.

### Prerequisites

- Tested on `Ubuntu 20.04`,
- Docker with `buildx` support (if docker images are needed).
  [How-To](https://www.digitalocean.com/community/tutorials/how-to-install-and-use-docker-on-ubuntu-22-04)
- C++ build dependencies (local builds):
    ```console
    sudo apt-get update
    sudo apt-get install -y git cmake g++ build-essential jq
    ```
- boost dev packages (local builds):
    ```console
    sudo apt-get install -y --no-install-recommends libboost-atomic-dev libboost-system-dev libboost-thread-dev libboost-filesystem-dev
    ```
- QEMU and binfmt-support (if you need to build/run arm64 images):
    ```console
    docker run --rm --privileged multiarch/qemu-user-static:register
    ```
  **NOTE:** `aarch64` vsomeip binaries do not work in QEMU.


### Build Docker images

You can use `./docker-build.sh` script:
```console
USAGE: ./docker-build.sh [OPTIONS] TARGETS

Standalone build helper for Hello Service/Client container.

OPTIONS:
  -l, --local      local docker import (does not push to ttl.sh)
  -v, --verbose    enable plain docker output and disable cache
      --help       show help

TARGETS:
  amd64|arm64    Target arch to build for (default: amd64)
```
By default it pushes the image to `ttl.sh` with 24h validity (could be extended to use github actions if needed)

### Building with cmake

```console
mkdir build
cd build/
cmake -DCMAKE_INSTALL_PREFIX=`pwd`/install ..
cmake --build . -j --target install
```
**NOTE:** Provided start scripts are meant to be executed from `${CMAKE_INSTALL_PREFIX}/bin`

## Running Hello examples

`vsomeip` has local (Unix sockets based) and remote (TCP/UDP) endpoint mode.

To enable remote endpoint mode config files (e.g. [hello_service.json](./config/hello_service.json)) and set host IP as `unicast` value.
This is automatically handled by setup scripts in `bin/` (e.g. [setup-service.sh](./bin/setup-service.sh)).

Host IP is replaced by `jq`.

For more details about arguments / environment variables you can check `./hello_service --help` and `./hello_client --help`.

**NOTE:**
Due to lacking APIs to change TCP/UDP setup, `hello_service` is configured to offer both TCP and UDP ports for its service, otherwise it would need another config or `jq` replacement.

### Running Hello Service/Client in Docker network

You need to build local Docker image or use existing `ttl.sh` image (valid for 24h).
After successful build `./docker-build.sh` also shows example client/service startup commands.

Docker image includes both Hello Service and Client to test against each other.
By default docker [entrypoint](./bin/docker-entrypoint.sh) sets the environment and passes arguments for Hello Service,
but if you supply `--client` as 1st argument, it sets up the Hello Client.

Example usage (dumped from `./docker-build.sh`):
```console
### Image pushed to: ttl.sh/hello_service-amd64-b6c68d18-7ba8-492f-b88e-57a9c3b711a7:24h

- To run Hello Service:
  $ docker run -e DEBUG=0 -it --rm ttl.sh/hello_service-amd64-b6c68d18-7ba8-492f-b88e-57a9c3b711a7:24h --timers 1s:1,1m:1,10ms:1,1ms:1

- To run Hello Client:
  $ docker run -e DEBUG=0 -it --rm ttl.sh/hello_service-amd64-b6c68d18-7ba8-492f-b88e-57a9c3b711a7:24h --client --sub --req 100 DockerClient!

# If you need to connect to external SOME/IP add --network=host after 'docker run'
```

### Running standalone examples

Before running the binaries you can source the `setup-*.sh` helper scripts in [bin/](./bin).

Setup scrips set `VSOMEIP_*` environment for the respective example and modify it's config file `unicast` with the real host IP.

**NOTE:** Those scripts get installed from cmake, and it is better to run everything from `<INSTALL>/bin` directory.
Don't forget to source the script again after `cmake install`, it will also replace config files with default `unicast: 127.0.0.1` value.

#### Running Hello Service

By default only 1min and 1sec timers are enabled, if you want to enable all timers:
```console
cd ./build/install/bin
. setup-service.sh
./hello_service --timers 1m:1,1s:1,10ms:1,1ms:1
```

#### Running Hello client

**NOTE:** Due to vsomeip architecture if you want to run Service and the Client on the same host, you must use a dedicated "proxy" config, so the client routes through the service.

- Running client on a different host:
  ```console
  cd ./build/install/bin
  . setup-client.sh
  ./hello_client --req 100 "ClientHost" --sub
  ```
- Running client on a the same host (as hello service):
  ```console
  cd ./build/install/bin
  . setup-client-proxy.sh
  ./hello_client --req 100 "SharedHost" --sub
  ```
