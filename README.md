# OpenIFS BOINC control code

This respository contains the instructions and code for building the controlling application used for controlling the OpenIFS code in the climateprediction.net project.

To compile the controlling code you will need to download and build the BOINC code (this is available from: https://github.com/BOINC/boinc). For instructions on building this code see: https://boinc.berkeley.edu/trac/wiki/CompileClient. This code needs to be in the same directory as the OpenIFS controller code.

To compile the controller code on a Linux machine:

g++ openifs.cpp -I./boinc -I./boinc/lib -L./boinc/api -L./boinc/lib -L./boinc/zip -lboinc_api -lboinc -lboinc_zip -pthread -std=gnu++17 -o openifs_0.1_x86_64-pc-linux-gnu

This will create an executable that is the app that is imported into the BOINC environment alongside the OpenIFS executable. Now to run this the OpenIFS ancillary files along with the OpenIFS executable will need to be alongside, the command to run this in standalone mode is:

./openifs_0.1_x86_64-pc-linux-gnu gw3a 0.1 00001

The command line parameters: [1] compiled executable, [2] experiment id, [3] app version id, [4] workunit id.

The current version of OpenIFS this supports is: oifs40r1. The OpenIFS code is compiled separately and is installed alongside the OpenIFS controller in BOINC. To upgrade the controller code in the future to later versions of OpenIFS consideration will need to be made whether there are any changes to the command line parameters the compiled version of OpenIFS takes in, and whether there are changes to the structure and content of the supporting ancillary files.

Currently in the controller code the following variables are fixed (this will change with further development):

OIFS_DUMMY_ACTION=abort

OMP_NUM_THREADS=1

OMP_SCHEDULE=STATIC

DR_HOOK=1

DR_HOOK_HEAPCHECK=no

DR_HOOK_STACKCHECK=no

OMP_STACKSIZE=128M

OIFS_RUN=1 (run number)

NPROC=8 (number of MPI tasks)

NTHREADS=1 (default number of OPENMP threads)

NAMELIST=fort.4 (NAMELIST file)
