# SA4U
SA4U is a static analyzer for detecting unit conversion errors in UAV source code.

## Directory Layout
There are currently 2 directories:
* `platforms/` contains Dockerfiles for building an environment to
  analyze supported UAV systems. Currently, we support ArduPilot. We
  intend to eventually support PX4. Each system has its own
  subdirectory, e.g. `platforms/ArduPilot`.
* `src/` contains the source code for SA4U. 

## Important Notes
The analysis process generates a lot of data. You may run out of
memory. Increase your swap size if this occurs. SA4U is designed to
use memory in a way that swapping should not slow down the analysis too much.

## Build Steps
1. Clone this repository:
```
> git clone https://github.com/obicons/sa4u
```
2. Change directories into sa4u (`cd sa4u`). Initialize all dependencies:
```
> git submodule update --init --recursive
```
3. Build a platform to analyze. Have a coffee & browse HN. ArduPilot has many dependencies, so this will take a
while. 
```
> docker image build -t sa4u-ardupilot ./platforms/ArduPilot
```
4. Change the permissions of the source code repository so that the
   Docker container's user can read/write:
```
> chmod -R a+rw ./
```
5. Start an analysis container:
```
> docker container run -ti -v $(pwd):/home/ardupilot/sa4u sa4u-ardupilot bash
```
6. In the analysis container, build SA4U:
```
> cd /home/ardupilot/sa4u/src/
> make
```
7: Run the analysis:
```
> ./sa4u                                                    \
	--compilation-database ../../ardupilot/build/sitl/      \
	--mavlink-definitions ../platforms/ArduPilot/common.xml \
	--prior-types ../platforms/ArduPilot/sample.json
```
