#!/bin/bash

set -eou pipefail

cp ../../subjects/ardupilot/libraries/GCS_MAVLink/GCS_Common.cpp tmp.cpp
cp ./GCS_Common.cpp ../../subjects/ardupilot/libraries/GCS_MAVLink/GCS_Common.cpp

docker container run                                                  \
       -v "$(pwd)/../../subjects/ardupilot/":/src/                    \
       -v "$(pwd)/compile_commands.json":/compile_commands.json       \
       -v "$(pwd)/../../platforms/ArduPilot/common.xml":/common.xml   \
       -v "$(pwd)/../../platforms/ArduPilot/sample.json":/sample.json \
       --rm                                                           \
       sa4u_z3 -c / -m /common.xml -p /sample.json

cp tmp.cpp ../../subjects/ardupilot/libraries/GCS_MAVLink/GCS_Common.cpp
rm tmp.cpp
