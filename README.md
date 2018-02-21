# S6: Elastic Scaling of Stateful Network Functions

# Contains
- S6 runtime library (`/core`) and souce-to-source compiler (`/gen_source`)
- A sample NF controller (`/s6ctl`)
- Sample applications (`/sample`)

# Running environment
- We have tested S6 in Ubuntu Server 16.04 LTS
- An NF instance can run as a process or a container
- We used Amazon EC2 for elastic scaling experiments (c4.xlarge)

# Compiling S6 library

## Prerequisites: basic utilities
- gcc, g++, make
- python 2.7
- python-clang-3.6 (s2s compiler)
- libclang-3.6-dev (s2s compiler)
- linux-headers (DPDK)
- unzip (Rapidjson)
- libssl-dev (sample applications)
- libpcre3-dev (DPI)
- libpcap-dev (PRAD)

## Prerequisites: 3rd-party libraries
- C++ BOOST
- Rapidjson
- DPDK
- BESS (for the sample NF controller)

### Building dependent libraries (DPDK, C++ BOOST, Rapidjson)
$ ./build.py build deps

### Building BESS
https://github.com/NetSys/bess

## Building main library
$ ./build.py

# Running S6

## Running S6 controller
$ ./s6ctl/controller.py

$ ./s6ctl
