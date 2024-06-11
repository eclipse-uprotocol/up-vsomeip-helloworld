# up-vsomeip-helloworld

- [up-vsomeip-helloworld](#up-vsomeip-helloworld)
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
  - [Hello Examples Customization options](#hello-examples-customization-options)
    - [Hello Service Runtime customization](#hello-service-runtime-customization)
      - [Updating Service JSON configuration](#updating-service-json-configuration)
    - [Hello Client Runtime customization](#hello-client-runtime-customization)
    - [Docker Entrypoint customization](#docker-entrypoint-customization)

## Overview

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

## Hello Examples Customization options

Sometimes it is useful to quickly test another SOME/IP client or service, or to play with major/minor versions and service discovery.

For that reason Service and Client support a list of  `UP_XXX` environment variables, that modify application's SOME/IP configuration without rebuilding the binaries.

### Hello Service Runtime customization

Current list of environment variables (and default values) can be checked in Hello Service help:

``` console
Usage: ./hello_service {OPTIONS}

OPTIONS:
  --tcp           Use reliable Some/IP endpoints. (NOTE: needs setting 'reliable' port in json config)
  --udp           Use unreliable Some/IP endpoints. Default:true
  --timers <LIST> Enable HelloService events. List: [ID:ENABLED,ID:ENABLED,...], where ID:[1s,1m,10ms,1ms], ENABLED:[0,1]
                  Defaults: 1m:1,1s:1,10ms:0,1ms:0

ENVIRONMENT:
  TIMERS          Enabled timer list (same as --timers). Default: 1m:1,1s:1,10ms:0,1ms:0
  DEBUG           Controls App verbosity (0=info, 1=debug, 2=trace). Default: 1
  TOGGLE_OFFER    (experimental) If set, toggles service offered state periodically. Default: disabled
  TIMER_CB_US     (experimental) Timer callback maximum delay (microseconds). Default: 0=disabled
  TIMER_DEBUG     (experimental) Timer debug level. Default: 0=disabled
  NO_TIMERS       (experimental) if set, disables timers and sends tmer events without any delay.

  UP_SERVICE          Use specified u16 value for HelloService service_id.    Default 0x6000
  UP_INSTANCE         Use specified u16 value for HelloService instance_id.   Default 0x0001
  UP_SERVICE_MAJOR    Use specified  u8 value for HelloService major version. Default 0
  UP_SERVICE_MINOR    Use specified u32 value for HelloService minor version. Default 0
  UP_METHOD           Use specified u16 value for HelloService method_id.     Default 0x8001
  UP_EVENTGROUP       Use specified u16 value for HelloService eventgroup_id. Default 0x8005
  UP_EVENT            Use specified u16 value for HelloService event_id.      Default 0x8005
  UP_SERVICES         Use specified list of alternative HelloService service_id. e.g. "0x6000,0x60001", Default: N/A
```

**NOTES:**

- `UP_XXX` allow overriding SOME/IP defaults in application code, but they also need matching json service configuration.
- `UP_SERVICES` must be set, in order to handle more than one `ServiceID` as Hello Service ID (e.g. `UP_SERVICES=0x100,0x101` allows 0x100 and 0x101 Service IDs to be accepted for Hello Service). `setup-service.sh` and `docker-entrypoint.sh` do this automatically.

#### Updating Service JSON configuration

Helper generator script is added (mainly for docker use-case), that modifies the 1st service definition from supplied json config.

``` console
USAGE: ./gen-services.sh INPUT SERVICE_COUNT [OUTPUT]

Helper for regeneration of Hello Service json config. Allows adding multiple services, changing default service id, instance id, ports, etc.

ARGS:
  INPUT           vsomeip config json file
  SERVICE_COUNT   number of services to generate [1..]
  OUTPUT          vsomeip config output file. If not set, dumps to stdout.

OPTIONS:
  --help           show help

ENVIRONMENT:
  UP_SERVICE       Update HelloService service_id in template.
  UP_INSTANCE      Update HelloService instance_id in template.
  UP_TCP           Update HelloService TCP port in template.
  UP_UDP           Update HelloService UDP port in template.
  UP_UNICAST       Update Host IP in config.
  UP_SD_MULTICAST  Update Service Discovery multicast address in config.
  UP_SD_PORT       Update Service Discovery port in config.
  UP_SD_TTL        Update Service Discovery TTL in config.
```

*NOTES:*

- This script modifies some parts of `service-discovery` configuration, default Service/Instance IDs, TCP/UDP port configuration.
- If `SERVICE_COUNT > 1` the script applies `UP_XXX` properties to default template, then clones the service template, so it contains specified count of services (each with incremented `ServiceID`). This is useful to simulate bigger network (load service discovery)

### Hello Client Runtime customization

Current list of environment variables (and default values) can be checked in Hello Client help:

``` console
Usage: ./hello_client {OPTIONS} {NAME}

NAME:
            If set, Calls HelloService::SayHello(NAME)

OPTIONS:
  --tcp     Use reliable Some/IP endpoints
  --udp     Use unreliable Some/IP endpoints. Default:true

  --sub     Subscribe for HelloService events
  --req N   Sends Hello request N times
  --inst ID Use specified instance_id for hello service.

ENVIRONMENT:

  DEBUG           Controls App verbosity (0=info, 1=debug, 2=trace). Default: 1
  QUIET           1=mute all debug/info messages. Default: 0
  DELTA           (benchmark) max delta (ms) from previous timer event. If exceeded dumps Delta warning. Default: 0
  DELAY           ms to wait after sending a SayHello() request (Do not set if benchmarking). Default: 0

  UP_SERVICE          Use specified u16 value for HelloService service_id.    Default 0x6000    [-1=ANY]
  UP_INSTANCE         Use specified u16 value for HelloService instance_id.   Default 0x0001    [-1=ANY]
  UP_SERVICE_MAJOR    Use specified  u8 value for HelloService major version. Default 0         [-1=ANY]
  UP_SERVICE_MINOR    Use specified u32 value for HelloService minor version. Default 0         [-1=ANY]
  UP_METHOD           Use specified u16 value for HelloService method_id.     Default 0x8001
  UP_EVENTGROUP       Use specified u16 value for HelloService eventgroup_id. Default 0x8005
  UP_EVENT            Use specified u16 value for HelloService event_id.      Default 0x8005
```

**NOTE:** The client does not need Service specific json configuration changes, but `service-discovery` configuration *MUST* be synchronized with the service.

**NOTE:** The client *MAY* be used for wildcard experiments e.g.

- Request ANY instance of `ServiceID: 0x1234`, then first `InstanceID` that is offered is assumed to be in use:

  ``` console
  UP_SERVICE=0x1234 UP_INSTANCE=-1 ./hello_client
  ```

- Request ANY major/minor version for `Service:0x1234 Instance:1`

  ``` console
  UP_SERVICE=0x1234 UP_INSTANCE=1 UP_SERVICE_MAJOR=-1 UP_SERVICE_MINOR=-1 ./hello_client
  ```

### Docker Entrypoint customization

Docker entrypoint [script](./bin/docker-entrypoint.sh) also supports `UP_XXX` variables and optional configuration update:

``` console
Usage: docker-entrypoint.sh [args]     Launch Hello Service with optional arguments.
      [--client] [args]                Launch Hello Client instead of Hello Service, with remaining arguments.
      [--gen <count>]                  Regenerate Service config with optional service count, applying UP_* env vars.
```

**NOTE:**

- `--gen` should be used for service use-case, must be the 1st and 2nd argument, followed by optional `hello_service` arguments.
- `UP_SD_XXX` options for service-discovery are also updated in the configuration for `--client` use-case.

**Example usage:**

To make Hello Service compatible for testing with example UEs from `up-client-vsomeip-cpp`:

- Create custom configuration for the `mE_rpcServer` service, e.g. in `mE-rpc-srv.env`:

  ``` shell
  # UP Service
  UP_SERVICE=0x1102
  UP_INSTANCE=0x1
  UP_SERVICE_MAJOR=0
  UP_SERVICE_MINOR=0
  UP_METHOD=0x0102

  # events not actually used
  UP_EVENTGROUP=0x8101
  UP_EVENT=0x8101

  UP_UDP=30509

  # ServiceDiscovery
  UP_SD_MULTICAST=224.224.224.245
  UP_SD_PORT=30490
  UP_SD_TTL=3
  ```

- Create custom configuration to replace `mE_subscriber` client, e.g. in `uE-rpc-cli.env`:
  
  ``` shell
  # UP Service
  UP_SERVICE=0x1104
  UP_INSTANCE=0x1
  UP_SERVICE_MAJOR=0
  UP_SERVICE_MINOR=0
  # method not used
  UP_METHOD=0x0

  # NOTE: event payload not compatible
  UP_EVENTGROUP=0x8100
  UP_EVENT=0x8100

  UP_UDP=30509

  # ServiceDiscovery
  UP_SD_MULTICAST=224.224.224.245
  UP_SD_PORT=30490
  UP_SD_TTL=3
  ```

- Start docker with custom environment for the service/client:

  ``` shell
  # to apply UE Service config:
  docker run --env-file mE-rpc-srv.env ... --gen 1 <service args>

  # to apply UE Client config:
  docker run --env-file mE-rpc-cli.env ... <client args>
  ```
