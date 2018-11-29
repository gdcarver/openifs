# OpenIFS

This respository contains the instructions and code for building the controlling application used for controlling the OpenIFS code in the climateprediction.net project.

To compile the controlling code you will need to download and build the BOINC code (this is available from: https://github.com/BOINC/boinc). For instructions on building this code see: https://boinc.berkeley.edu/trac/wiki/CompileClient. This code needs to be in the same directory as the OpenIFS controller code.

To compile the code:

g++ openifs_0.1.cpp -I./boinc -I./boinc/lib -L./boinc/api -L./boinc/lib -L./boinc/zip -lboinc_api -lboinc -lboinc_zip -pthread -std=gnu++17 -lstdc++fs -o openifs_0.1_x86_64-pc-linux-gnu

This will create an executable that is the app that is imported into the BOINC environment along with the OpenIFS executable.
