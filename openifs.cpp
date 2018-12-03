//
// Control code for the OpenIFS application in the climateprediction.net project
//
// Written by Andy Bowery (Oxford eResearch Centre, Oxford University) November 2018
//

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <chrono>
#include <thread>
#include <fstream>
#include <iostream>
#include <exception>
#include "./boinc/api/boinc_api.h"
#include "./boinc/zip/boinc_zip.h"

#ifndef _MAX_PATH
   #define _MAX_PATH 512
#endif

int main(int argc, char** argv) {
    using namespace std::chrono;
    using namespace std::this_thread;
    using namespace std;
    int retval = 0;

    // Defaults to input arguments
    int OIFS_RUN=1;                   // run number, output will be saved to directory: output$OIFS_RUN
    int OIFS_RES;	              // model resolution - not used
    std::string OIFS_EXPID;           // model experiment id, must match string in filenames
    std::string OIFS_EXE;             // OpenIFS executable - not used
    int NPROC=8;                      // number of MPI tasks, need to be set in fort.4 as well
    int NTHREADS=1;                   // default number of OPENMP threads
    std::string NAMELIST="fort.4";    // NAMELIST file, this name is fixed
    int TIMESTEP;                     // size of the timestep
    int FCLEN;                        // number of days of the model run

    boinc_init();
    boinc_parse_init_data_file();

    // Get BOINC user preferences
    APP_INIT_DATA dataBOINC;
    boinc_get_init_data(dataBOINC);

    // Set BOINC optional values
    BOINC_OPTIONS options;
    boinc_options_defaults(options);
    options.main_program = true;
    options.multi_process = true;
    options.check_heartbeat = true;
    options.handle_process_control = true;  // monitor handles all suspend/quit/resume
    options.direct_process_action = false;  // monitor won't get suspended/killed by boinc
    options.send_status_msgs = false;

    retval = boinc_init_options(&options);
    if (retval) {
       fprintf(stderr, "BOINC init options failed!\n");
    }

    // Get the project path
    std::string project_path = dataBOINC.project_dir + std::string("/");
    fprintf(stderr,"current project directory is: %s\n", project_path.c_str());

    // Get the slots path (the current working path)
    char slot_path[256];
    if (getcwd(slot_path, sizeof(slot_path)) == NULL)
      fprintf(stderr,"getcwd() returned an error\n");
    else
      fprintf(stderr,"current working directory is: %s\n", slot_path);

    fprintf(stderr, "(argv0) %s\n", argv[0]);
    fprintf(stderr, "(argv1) exptid: %s\n", argv[1]);
    fprintf(stderr, "(argv2) version: %s\n", argv[2]);
    fprintf(stderr, "(argv3) wuid: %s\n", argv[3]);
    fflush(stderr);

    // Read the exptid, batchid, version, wuid from the command line
    std::string exptid  = argv[1];
    std::string version = argv[2];
    std::string wuid    = argv[3];
    OIFS_EXPID = exptid;

    boinc_begin_critical_section();

    // Copy apps files to working directory
    std::string var1 = std::string("cp ") + project_path + \
                       std::string("openifs_app_") + version + std::string(".zip ") + slot_path;
    fprintf(stderr, "Copying apps to working directory: %s\n", var1.c_str());
    fflush(stderr);
    system(var1.c_str());

    // Unzip the app zip file
    std::string var2 = slot_path + std::string("/openifs_app_") + version + std::string(".zip");
    fprintf(stderr, "Unzipping app zip file: %s\n", var2.c_str());
    fflush(stderr);
    boinc_zip(UNZIP_IT,var2.c_str(),slot_path);

    // Make the ifsdata directory
    std::string var3 = slot_path + std::string("/openifs_app/ifsdata");
    if (mkdir(var3.c_str(),S_IRWXU|S_IRWXG|S_IROTH|S_IXOTH) != 0) fprintf(stderr, "mkdir() ifsdata failed\n");


    // Copy namelist files to working directory
    std::string var4 = std::string("cp ") + project_path + \
                       std::string("openifs_wu_") + exptid + std::string(".zip ") + slot_path;
    fprintf(stderr, "Copying namelist files to working directory: %s\n", var4.c_str());
    fflush(stderr);
    system(var4.c_str());

    // Unzip the namelist zip file
    std::string var5 = slot_path + std::string("/openifs_wu_") + exptid + std::string(".zip");
    fprintf(stderr, "Unzipping namelist zip file: %s\n", var5.c_str());
    fflush(stderr);
    boinc_zip(UNZIP_IT,var5.c_str(),slot_path);

    // Move workunit files to app directory
    std::string var6 = std::string("mv ") + slot_path + std::string("/openifs_wu_") + exptid + \
                       std::string("/* ") + slot_path + std::string("/openifs_app");
    fprintf(stderr, "Move workunit files to app directory: %s\n", var6.c_str());
    fflush(stderr);
    system(var6.c_str());


    // Parse the fort.4 namelist for the filenames and variables
    std::string namelist_file = slot_path + std::string("/openifs_app/") + NAMELIST;

    const char strSearch[8][22]={"!GHG_FILE=","!IC_ANCIL_FILE=","!CLIMATE_DATA_FILE=","!WAVE_DATA_FILE=","!HORIZ_RESOLUTION=","!GRID_RESOLUTION=","!FCLEN=","!TIMESTEP="};
    char* strFind[8] = {NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL};
    char strCpy[8][_MAX_PATH];
    char strTmp[_MAX_PATH];
    memset(strCpy,0x00,8*_MAX_PATH);
    memset(strTmp, 0x00, _MAX_PATH);
    FILE* fParse = boinc_fopen(namelist_file.c_str(),"r");
    
    std::string GHG_FILE, IC_ANCIL_FILE, CLIMATE_DATA_FILE, WAVE_DATA_FILE;
    int HORIZ_RESOLUTION, GRID_RESOLUTION;

    if (fParse) {
       fseek(fParse, 0x00, SEEK_SET); // go to top of file
       memset(strTmp, 0x00, _MAX_PATH);
       while (! feof(fParse)) {
           memset(strTmp, 0x00, _MAX_PATH);
           fgets(strTmp, _MAX_PATH-1, fParse);
           if (!strFind[0]) {
               strFind[0] = strstr(strTmp, strSearch[0]);
               if (strFind[0]) {
                   strcpy(strCpy[0], strFind[0]);
               }
            }
            if (!strFind[1]) {
                strFind[1] = strstr(strTmp, strSearch[1]);
                if (strFind[1]) {
                    strcpy(strCpy[1], strFind[1]);
		}
            }
            if (!strFind[2]) {
                strFind[2] = strstr(strTmp, strSearch[2]);
                if (strFind[2]) {
                    strcpy(strCpy[2], strFind[2]);
                }
            }
            if (!strFind[3]) {
                strFind[3] = strstr(strTmp, strSearch[3]);
                if (strFind[3]) {
                    strcpy(strCpy[3], strFind[3]);
                }
            }
            if (!strFind[4]) {
                strFind[4] = strstr(strTmp, strSearch[4]);
                if (strFind[4]) {
                    strcpy(strCpy[4], strFind[4]);
                }
            }
            if (!strFind[5]) {
                strFind[5] = strstr(strTmp, strSearch[5]);
                if (strFind[5]) {
                    strcpy(strCpy[5], strFind[5]);
                }
            }
            if (!strFind[6]) {
                strFind[6] = strstr(strTmp, strSearch[6]);
                if (strFind[6]) {
                    strcpy(strCpy[6], strFind[6]);
                }
            }
            if (!strFind[7]) {
                strFind[7] = strstr(strTmp, strSearch[7]);
                if (strFind[7]) {
                    strcpy(strCpy[7], strFind[7]);
                }
            }
            if (strFind[0] && strFind[1] && strFind[2] && strFind[3] && strFind[4] && strFind[5] && strFind[6] && strFind[7]) {
                break;
            }
       }
       // OK, either feof or we hit the string		
       if (strCpy[0][0] != 0x00) {
            memset(strTmp, 0x00, _MAX_PATH);
            strncpy(strTmp, (char*)(strCpy[0] + strlen(strSearch[0])), 100);
            //std::string GHG_FILE = strTmp; 
            GHG_FILE = strTmp;
            while(!GHG_FILE.empty() && std::isspace(*GHG_FILE.rbegin())) GHG_FILE.erase(GHG_FILE.length()-1);
            fprintf(stderr, "GHG_FILE: %s\n", GHG_FILE.c_str());
       }
       if (strCpy[1][0] != 0x00) {
            memset(strTmp, 0x00, _MAX_PATH);
            strncpy(strTmp, (char*)(strCpy[1] + strlen(strSearch[1])), 100);
            IC_ANCIL_FILE = strTmp; 
            while(!IC_ANCIL_FILE.empty() && std::isspace(*IC_ANCIL_FILE.rbegin())) IC_ANCIL_FILE.erase(IC_ANCIL_FILE.length()-1);
            fprintf(stderr, "IC_ANCIL_FILE: %s\n", IC_ANCIL_FILE.c_str());
       }
       if (strCpy[2][0] != 0x00) {
            memset(strTmp, 0x00, _MAX_PATH);
            strncpy(strTmp, (char*)(strCpy[2] + strlen(strSearch[2])), 100);
            CLIMATE_DATA_FILE = strTmp; 
            while(!CLIMATE_DATA_FILE.empty() && std::isspace(*CLIMATE_DATA_FILE.rbegin())) CLIMATE_DATA_FILE.erase(CLIMATE_DATA_FILE.length()-1);
            fprintf(stderr, "CLIMATE_DATA_FILE: %s\n", CLIMATE_DATA_FILE.c_str());
       }
       if (strCpy[3][0] != 0x00) {
            memset(strTmp, 0x00, _MAX_PATH);
            strncpy(strTmp, (char*)(strCpy[3] + strlen(strSearch[3])), 100);
            WAVE_DATA_FILE = strTmp; 
            while(!WAVE_DATA_FILE.empty() && std::isspace(*WAVE_DATA_FILE.rbegin())) WAVE_DATA_FILE.erase(WAVE_DATA_FILE.length()-1);
            fprintf(stderr, "WAVE_DATA_FILE: %s\n", WAVE_DATA_FILE.c_str());
       }
       if (strCpy[4][0] != 0x00) {
            HORIZ_RESOLUTION=atoi(strCpy[4] + strlen(strSearch[4]));
            fprintf(stderr, "HORIZ_RESOLUTION: %i\n", HORIZ_RESOLUTION);
       }
       if (strCpy[5][0] != 0x00) {
            GRID_RESOLUTION=atoi(strCpy[5] + strlen(strSearch[5]));
            fprintf(stderr, "GRID_RESOLUTION: %i\n", GRID_RESOLUTION);
       }
       if (strCpy[6][0] != 0x00) {
            FCLEN=atoi(strCpy[6] + strlen(strSearch[6]));
            fprintf(stderr, "FCLEN: %i\n", FCLEN);
       }
       if (strCpy[7][0] != 0x00) {
            TIMESTEP=atoi(strCpy[7] + strlen(strSearch[7]));
            fprintf(stderr, "TIMESTEP: %i\n", TIMESTEP);
       }
       fclose(fParse);
    }


    // Copy the IC ancils to working directory
    std::string var7 = std::string("cp ") + project_path + IC_ANCIL_FILE + \
                       std::string(".zip ") + slot_path;
    fprintf(stderr, "Copying IC ancils to working directory: %s\n", var7.c_str());
    fflush(stderr);
    system(var7.c_str());

    // Unzip the IC ancils zip file
    std::string var8 = slot_path + std::string("/") + IC_ANCIL_FILE + std::string(".zip");
    fprintf(stderr, "Unzipping IC ancils zip file: %s\n", var8.c_str());
    fflush(stderr);
    boinc_zip(UNZIP_IT,var8.c_str(),slot_path);

    // Move IC ancils to app directory
    std::string var9 = std::string("mv ") + slot_path + std::string("/") + IC_ANCIL_FILE + \
                       std::string("/* ") + slot_path + std::string("/openifs_app");
    fprintf(stderr, "Move IC ancil to app directory: %s\n",var9.c_str());
    fflush(stderr);
    system(var9.c_str());


    // Copy the wave data file to working directory
    std::string var10 = std::string("cp ") + project_path + WAVE_DATA_FILE + \
                        std::string(".zip ") + slot_path;
    fprintf(stderr, "Copying the wave data file to working directory: %s\n", var10.c_str());
    fflush(stderr);
    system(var10.c_str());

    // Unzip the wave data zip file
    std::string var11 = slot_path + std::string("/") + WAVE_DATA_FILE + std::string(".zip");
    fprintf(stderr, "Unzipping the wave data zip file: %s\n", var11.c_str());
    fflush(stderr);
    boinc_zip(UNZIP_IT,var11.c_str(),slot_path);

    // Move wave data files to app directory
    std::string var12 = std::string("mv ") + slot_path + std::string("/") + WAVE_DATA_FILE + \
                        std::string("/* ") + slot_path + std::string("/openifs_app");
    fprintf(stderr, "Moving the wave data files to app directory: %s\n", var12.c_str());
    fflush(stderr);
    system(var12.c_str());


    // Copy the GHG ancils to working directory
    std::string var13 = std::string("cp ") + project_path + GHG_FILE + \
                        std::string(".zip ") + slot_path;
    fprintf(stderr, "Copying GHG ancils to working directory: %s\n", var13.c_str());
    fflush(stderr);
    system(var13.c_str());

    // Unzip the GHG ancils zip file
    std::string var14 = slot_path + std::string("/") + GHG_FILE + std::string(".zip");
    fprintf(stderr, "Unzipping GHG ancils zip file: %s\n", var14.c_str());
    fflush(stderr);
    boinc_zip(UNZIP_IT,var14.c_str(),slot_path);

    // Move GHG ancil files to app directory
    std::string var15 = std::string("mv ") + slot_path + std::string("/") + GHG_FILE + \
                        std::string("/* ") + slot_path + std::string("/openifs_app/ifsdata");
    fprintf(stderr, "Moving GHG ancils to app directory: %s\n", var15.c_str());
    fflush(stderr);
    system(var15.c_str());


    // Copy the climate data file to working directory
    std::string var16 = std::string("cp ") + project_path + CLIMATE_DATA_FILE + \
                        std::string(".zip ") + slot_path;
    fprintf(stderr, "Copying the climate data file to working directory: %s\n", var16.c_str());
    fflush(stderr);
    system(var16.c_str());

    // Unzip the climate data zip file
    std::string var17 = slot_path + std::string("/") + CLIMATE_DATA_FILE + std::string(".zip");
    fprintf(stderr, "Unzipping the climate data zip file: %s\n", var17.c_str());
    fflush(stderr);
    boinc_zip(UNZIP_IT,var17.c_str(),slot_path);

    // Make the climate data directory
    std::string var18 = slot_path + std::string("/openifs_app/") + \
                       std::to_string(HORIZ_RESOLUTION) + std::string("l_") + std::to_string(GRID_RESOLUTION);
    if (mkdir(var18.c_str(),S_IRWXU|S_IRWXG|S_IROTH|S_IXOTH) != 0) fprintf(stderr, "mkdir() climate data failed\n");

    // Move climate data files to app directory
    std::string var19 = std::string("mv ") + slot_path + std::string("/") + CLIMATE_DATA_FILE + \
                        std::string("/* ") + slot_path + std::string("/openifs_app/") + \
                        std::to_string(HORIZ_RESOLUTION) + std::string("l_") + std::to_string(GRID_RESOLUTION);
    fprintf(stderr, "Moving the climate data files to app directory: %s\n", var19.c_str());
    fflush(stderr);
    system(var19.c_str());


    char *pathvar;
    // Set the GRIB_SAMPLES_PATH environmental variable
    std::string var20 = std::string("GRIB_SAMPLES_PATH=") + slot_path + \
                        std::string("/openifs_app/eccodes/ifs_samples/grib1_mlgrib2");
    if (putenv((char *)var20.c_str())) {
      fprintf(stderr, "putenv failed \n");
      return 1;
    }
    pathvar = getenv("GRIB_SAMPLES_PATH");
    fprintf(stderr, "The current GRIB_SAMPLES_PATH is: %s\n", pathvar);

    // Set the GRIB_DEFINITION_PATH environmental variable
    std::string var21 = std::string("GRIB_DEFINITION_PATH=") + slot_path + \
                        std::string("/openifs_app/eccodes/definitions");
    if (putenv((char *)var21.c_str())) {
      fprintf(stderr, "putenv failed \n");
      return 1;
    }
    pathvar = getenv("GRIB_DEFINITION_PATH");
    fprintf(stderr, "The current GRIB_DEFINITION_PATH is: %s\n", pathvar);

    // Change permissions on the app directory
    std::string var22 = std::string("chmod -R 777 ") + slot_path + std::string("/openifs_app");
    fprintf(stderr, "Changing permissions: %s\n", var22.c_str());
    fflush(stderr);
    system(var22.c_str());

    // Change directory to the app directory
    std::string var23 = slot_path + std::string("/openifs_app/");
    if (chdir(var23.c_str()) != 0) fprintf(stderr, "chdir() failed to: %s\n",var23.c_str());

    // Set the OIFS_DUMMY_ACTION environmental variable, this controls what OpenIFS does if it goes into a dummy subroutine
    // Possible values are: 'quiet', 'verbose' or 'abort'
    std::string var24 = std::string("OIFS_DUMMY_ACTION=abort");
    if (putenv((char *)var24.c_str())) {
      fprintf(stderr, "putenv failed \n");
      return 1;
    }
    pathvar = getenv("OIFS_DUMMY_ACTION");
    fprintf(stderr, "The current OIFS_DUMMY_ACTION is: %s\n", pathvar);

    // Set the OMP_NUM_THREADS environmental variable, the number of threads
    std::string var25 = std::string("OMP_NUM_THREADS=") + std::to_string(NTHREADS);
    if (putenv((char *)var25.c_str())) {
      fprintf(stderr, "putenv failed \n");
      return 1;
    }
    pathvar = getenv("OMP_NUM_THREADS");
    fprintf(stderr, "The current OMP_NUM_THREADS is: %s\n", pathvar);

    // Set the OMP_SCHEDULE environmental variable, this enforces static thread scheduling
    std::string var26 = std::string("OMP_SCHEDULE=STATIC");
    if (putenv((char *)var26.c_str())) {
      fprintf(stderr, "putenv failed \n");
      return 1;
    }
    pathvar = getenv("OMP_SCHEDULE");
    fprintf(stderr, "The current OMP_SCHEDULE is: %s\n", pathvar);

    // Set the DR_HOOK environmental variable, this controls the tracing facility in OpenIFS, off=0 and on=1
    std::string var27 = std::string("DR_HOOK=1");
    if (putenv((char *)var27.c_str())) {
      fprintf(stderr, "putenv failed \n");
      return 1;
    }
    pathvar = getenv("DR_HOOK");
    fprintf(stderr, "The current DR_HOOK is: %s\n", pathvar);

    // Set the DR_HOOK_HEAPCHECK environmental variable, this ensures the heap size statistics are reported
    std::string var28 = std::string("DR_HOOK_HEAPCHECK=no");
    if (putenv((char *)var28.c_str())) {
      fprintf(stderr, "putenv failed \n");
      return 1;
    }
    pathvar = getenv("DR_HOOK_HEAPCHECK");
    fprintf(stderr, "The current DR_HOOK_HEAPCHECK is: %s\n", pathvar);

    // Set the DR_HOOK_STACKCHECK environmental variable, this ensures the stack size statistics are reported
    std::string var29 = std::string("DR_HOOK_STACKCHECK=no");
    if (putenv((char *)var29.c_str())) {
      fprintf(stderr, "putenv failed \n");
      return 1;
    }
    pathvar = getenv("DR_HOOK_STACKCHECK");
    fprintf(stderr, "The current DR_HOOK_STACKCHECK is: %s\n", pathvar);

    // Set the OMP_STACKSIZE environmental variable, OpenIFS needs more stack memory per process
    std::string var30 = std::string("OMP_STACKSIZE=128M");
    if (putenv((char *)var30.c_str())) {
      fprintf(stderr, "putenv failed \n");
      return 1;
    }
    pathvar = getenv("OMP_STACKSIZE");
    fprintf(stderr, "The current OMP_STACKSIZE is: %s\n", pathvar);

    // Check for existence of NAMELIST
    struct stat buffer;
    if(NAMELIST != "fort.4") {
       fprintf(stderr, "NAMELIST file path is: %s\n",namelist_file.c_str());
       fflush(stderr);
       if (stat((char *)namelist_file.c_str(), &buffer) < 0){
          fprintf(stderr, "NAMELIST file does not exist: %s\n",NAMELIST.c_str());
       }
       // Rename the NAMELIST file to fort.4
       std::string var31 = std::string("mv ") + namelist_file + slot_path + std::string("/openifs_app/fort.4");
       fprintf(stderr, "Renaming NAMELIST file: %s\n", var31.c_str());
       fflush(stderr);
       system(var31.c_str());
    }
    if (stat((char *)namelist_file.c_str(), &buffer) < 0) {
       fprintf(stderr, "NAMELIST file does not exist: %s\n",NAMELIST.c_str());
    }

    // Set the core dump size to 0
    struct rlimit core_limits;
    core_limits.rlim_cur = core_limits.rlim_max = 0;
    if (setrlimit(RLIMIT_CORE, &core_limits) != 0) fprintf(stderr, "setting core dump size to 0 failed\n");

    // Set the stack limit to be unlimited
    struct rlimit stack_limits;
    stack_limits.rlim_cur = stack_limits.rlim_max = RLIM_INFINITY;
    if (setrlimit(RLIMIT_STACK, &stack_limits) != 0) fprintf(stderr, "setting stack limit to unlimited failed\n");

    // Set FCLEN_TIMESTEP
    std::string FCLEN_TIMESTEP=" -f d"+std::to_string(FCLEN)+" -t "+std::to_string(TIMESTEP);

    // Start the OpenIFS job
    std::string var32 = std::string("cd ") + slot_path + \
                        std::string("/openifs_app;./master.exe -e ") + exptid + FCLEN_TIMESTEP;
    fprintf(stderr, "Starting the executable: %s\n", var32.c_str());
    fflush(stderr);
    system(var32.c_str());

    sleep_until(system_clock::now() + seconds(5));

    // Make the results folder
    std::string var33 = slot_path + std::string("/openifs_result_") + wuid;
    if (mkdir(var33.c_str(),S_IRWXU|S_IRWXG|S_IROTH|S_IXOTH) != 0) fprintf(stderr, "mkdir() openifs_result failed\n");

    // Move the results to the results folder
    std::string var34 = std::string("mv -t ") + slot_path + std::string("/openifs_result_") + wuid + std::string(" ") + \
                        slot_path + std::string("/openifs_app/ICM*+* ") + slot_path + \
                        std::string("/openifs_app/NODE* ") + slot_path + \
                        std::string("/openifs_app/ifs.stat");
    fprintf(stderr, "Moving the results to the results folder: %s\n", var34.c_str());
    fflush(stderr);
    system(var34.c_str());

    boinc_end_critical_section();

    // Change directory to the slot path
    if (chdir(slot_path) != 0) fprintf(stderr, "chdir() failed to: %s\n",slot_path);

    // Tar the results files
    std::string var35 = std::string("tar cf openifs_result_") + wuid + std::string(".tar openifs_result_") + wuid + \
                        std::string("/ICM*+* openifs_result_") + wuid + std::string("/NODE* openifs_result_") + wuid + \
                        std::string("/ifs.stat");
    fprintf(stderr, "Tarring: %s\n", var35.c_str());
    fflush(stderr);
    system(var35.c_str());

    sleep_until(system_clock::now() + seconds(20));

    // Copy results files to script directory
    std::string var36 = std::string("cp ") + slot_path + \
                        std::string("/openifs_result_") + wuid + std::string(".tar ") + project_path;
    fprintf(stderr, "Copying results files to script directory: %s\n", var36.c_str());
    fflush(stderr);
    system(var36.c_str());

    sleep_until(system_clock::now() + seconds(20));

    fprintf(stderr, "Checkpoint 1\n");
    fflush(stderr);

    // Upload the results file
    std::string var39 = project_path + std::string("/openifs_result_") + wuid + std::string(".tar");
    fprintf(stderr, "File being uploaded: %s\n", var39.c_str());
    fflush(stderr);
    boinc_upload_file(var39);

    fprintf(stderr, "Checkpoint 2\n");
    fflush(stderr);

    boinc_finish(0);
    return 0;
}
