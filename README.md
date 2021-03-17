# SA4U
SA4U is a static analyzer for detecting unit conversion errors in UAV source code.

## Directory Layout
There are currently 2 directories:
* `platforms/` contains Dockerfiles for building an environment to
  analyze supported UAV systems. Currently, we support ArduPilot. We
  intend to eventually support PX4. Each system has its own
  subdirectory, e.g. `platforms/ArduPilot`.
* `src/` contains the source code for SA4U. 

## Build Steps

1. Clone this repository:
```
> git clone https://github.com/obicons/sa4u
```
2. Initialize all dependencies:
```
> git submodule update --init --recursive
```
3. Build a platform to analyze. Have a coffee & browse HN. ArduPilot has many dependencies, so this will take a
while. 
```
> docker image build -t sa4u-ardupilot ./platforms/ArduPilot
```
4. Start an analysis container:
```
> docker container run -ti -v $(pwd):/home/ardupilot/sa4u sa4u-ardupilot bash
```
5. In the analysis container, build SA4U:
```
> cd /home/ardupilot/sa4u/src/
> make
```