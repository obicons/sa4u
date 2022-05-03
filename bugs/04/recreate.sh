#!/bin/bash

set -eou pipefail

docker container run                                                  \
       -v "$(pwd)/../../subjects/ardupilot/":/src/                    \
       -v "$(pwd)/compile_commands.json":/compile_commands.json       \
       -v "$(pwd)/../../platforms/ArduPilot/common.xml":/common.xml   \
       -v "$(pwd)/../../platforms/ArduPilot/sample.json":/sample.json \
       --rm                                                           \
       sa4u_z3 -c / -m /common.xml -p /sample.json
