# SA4U
SA4U is a static analyzer for detecting unit conversion errors in UAV source code.

## Directory Layout
* `bugs/` - contains scripts to recreate bugs diagnosed by SA4U
* `demos/` - contains examples to analyze with SA4U
* `lsp/` - contains the SA4U VSCode extension
* `platforms/` - contains support data for SA4U to analyze special subjects
* `sa4u_z3/` - contains the source code for the latest SA4U

## Running on a Demo

### Step 1: Build Docker Image
```sh
$ (cd sa4u_z3 && docker image build -t sa4u ./)
```

### Step 2: Invoke SA4U on `demos/01`
```sh
$ docker container run        \
  -v "$(pwd)/demos/01":/src/  \
  sa4u                        \
  -m /src/CMASI.xml           \
  -p /src/ex_prior.json       \
  -c /src/compile_commands_dir
```

You should see no errors.

### Step 3: Insert an error in `demos/01`
Uncomment the first line in `demos/01` that indicates that there is an error. Invoke SA4U like you did in step 2. You should see output like this:
```
ERROR!
  afrl::cmasi::Location3D::getAltitude return unit known from CMASI definition
  afrl::cmasi::Location3D::getAltitude known from CMASI definition
  Variable z declared in /src/ex.cpp on line 25 (1)
  Call to set_alt_in_cm in /src/ex.cpp on line 29 column 3 (9)
  Call to set_alt_in_cm in /src/ex.cpp on line 31 column 3 (10)
```
