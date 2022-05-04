#!/bin/bash

set -eou pipefail

cp ../../subjects/px4/src/modules/mavlink/mavlink_receiver.cpp tmp.cpp
cp mavlink_receiver.cpp ../../subjects/px4/src/modules/mavlink/mavlink_receiver.cpp

docker container run                                                  \
       -v "$(pwd)/../../subjects/px4/":/src/                          \
       -v "$(pwd)/compile_commands.json":/compile_commands.json       \
       -v "$(pwd)/../../platforms/ArduPilot/common.xml":/common.xml   \
       -v "$(pwd)/../../platforms/PX4/sample.json":/sample.json       \
       --rm                                                           \
       sa4u_z3 -c / -m /common.xml -p /sample.json

cp tmp.cpp ../../subjects/px4/src/modules/mavlink/mavlink_receiver.cpp
rm tmp.cpp
