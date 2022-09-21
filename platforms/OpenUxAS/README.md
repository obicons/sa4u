# Analyzing OpenUxAS

### Document Conventions
- Code that appears like this:
  ```sh
  $ command
  ```
  is called a *prompt* and indicates to execute `command` in your shell.
- The prompt
  ```sh
  container$ command
  ```
  means to execute `command` in the most recently created container.

## CLI Analysis

### Step 1: Obtain OpenUxAS Source
You can obtain the OpenUxAS source code by running this command **outside** of this repository:
```sh
$ git clone https://github.com/afrl-rq/OpenUxAS
```

SA4U has been tested on the commit `a41e69c9d923ae4795a99fd8201a32f6bc81f4e1` so it is **highly** recommended to experiment using this commit:
```sh
$ cd OpenUxAS
$ export OPENUXAS_SHA=a41e69c9d923ae4795a99fd8201a32f6bc81f4e1
$ git checkout "$OPENUXAS_SHA"
```

### Step 2: Create the `sa4u-openuxas` Docker Image
Now, we need to build a special SA4U Docker image to analyze OpenUxAS. SA4U's default Docker image does not work with OpenUxAS because OpenUxAS's build system generates files whose paths need to be constant at analysis time. Meanwhile, since the default SA4U image doesn't have OpenUxAS's build dependencies, we need a custom image.

You can create the custom image by navigating to the directory that contains this README, and running this command:
```sh
$ docker image build -t sa4u-openuxas ./
```

### Step 3: Build OpenUxAS in a Container
Next, build OpenUxAS in a container. First, navigate to the directory where you clone OpenUxAS. Then, run these commands:
```sh
$ docker container run                  \
   --rm                                 \
   -ti                                  \
   -w /home/ardupilot/OpenUxAS          \
   -v "$(pwd)":/home/ardupilot/OpenUxAS \
   sa4u-openuxas                        \
   bash
container$ bear ./anod build uxas --force
```

### Step 4: Edit the `compile_commands.json` File
The default `compile_commands.json` file in the `OpenUxAS` repository is missing include flags. This is because this include path is set by OpenUxAS's build system through environment variables, so bear fails to correctly detect the setting. You can patch this for the main file by editing `compile_commands.json`. In particular, add these strings to the `arguments` array for the file `src/cpp/UxAS_Main.cpp`:
```
"-I../../uxas-lmcp-cpp/build",
"-I../../pugixml/install/include/pugixml.hpp",
```

### Step 5: Insert a Synthetic Bug
Edit the file `infrastructure/sbx/x86_64-linux/uxas-release/src/src/cpp/UxAS_Main.cpp` in the OpenUxAS directory to introduce a bug.

For example, outside of the `main` function declare an uninitialized global variable `alt_in_cm`. Then, inside of `main` after the line `auto o = new afrl::cmasi::AirVehicleState;`, insert the line `alt_in_cm = o->getLocation()->getAltitude();`.

### Step 6: Analyze
You're ready to detect your synthetic bug. Run these commands from the OpenUxAS directory:
```sh
$ docker container run                  \
   --rm                                 \
   -ti                                  \
   -v "$(pwd)":/home/ardupilot/OpenUxAS \
   sa4u-openuxas                        \
   bash
container$ ignore_flags=; for filename in $(cat ~/ignore_list); do ignore_flags="-i ${filename} ${ignore_flags}";	done
container$ python3.9 main.py -c ~/OpenUxAS/ -m ~/CMASI.xml -p ~/types.json $ignore_flags
```

You should see output like this:
```
ERROR!
  __x unit known from prior type file
  alt_in_cm unit known from prior type file
  alt_in_cm known from prior type file
  afrl::cmasi::Location3D::getAltitude return unit known from CMASI definition
  afrl::cmasi::Location3D::getAltitude known from CMASI definition
  Assignment to alt_in_cm in /home/ardupilot/OpenUxAS/infrastructure/sbx/x86_64-linux/uxas-release/src/src/cpp/UxAS_Main.cpp on line 110 column 13 (1245)
```

## VSCode Extension Analysis
Please follow the CLI instructions BEFORE proceeding.

### Step 1: Copy Settings File into OpenUxAS Clone
Copy the file `config.json` into the `.sa4u` directory in your OpenUxAS clone. For example, if you cloned OpenUxAS into `~/src/OpenUxAS/`, you will run:
```sh
$ mkdir ~/src/OpenUxAS/.sa4u/
$ cp config.json ~/src/OpenUxAS/.sa4u/
```

### Step 2: Initialize VSCode Extension Development Environment
Follow the instructions located [here](https://github.com/obicons/sa4u/tree/main/lsp).
