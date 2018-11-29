# OpenIFS BOINC control code

This respository contains the instructions and code for building the controlling application used for controlling the OpenIFS code in the climateprediction.net project.

To compile the controlling code you will need to download and build the BOINC code (this is available from: https://github.com/BOINC/boinc). For instructions on building this code see: https://boinc.berkeley.edu/trac/wiki/CompileClient. This code needs to be in the same directory as the OpenIFS controller code.

To compile the code on a Linux machine:

g++ openifs_0.1.cpp -I./boinc -I./boinc/lib -L./boinc/api -L./boinc/lib -L./boinc/zip -lboinc_api -lboinc -lboinc_zip -pthread -std=gnu++17 -o openifs_0.1_x86_64-pc-linux-gnu

This will create an executable that is the app that is imported into the BOINC environment along with the OpenIFS executable.

Now to run this the OpenIFS files will need to be alongside. An example command:

./openifs_0.4_x86_64-pc-linux-gnu gw3a 0 0.4 00001

Command line parameters: [2] experiment id, [3] batch id, [4] version id, [5] workunit id.

The current version of OpenIFS this supports is: oifs40r1

This code is compiled separately and is installed alongside the OpenIFS controller in BOINC. 

To upgrade this controller code in the future to later versions of OpenIFS consideration will need to be whether there are changes to the command line parameters the compiled version of OpenIFS takes in, and whether there are changes to the structure of the supporting ancillary files.


