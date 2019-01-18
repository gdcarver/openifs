//
// Control code for the OpenIFS application in the climateprediction.net project
//
// Written by Andy Bowery (Oxford eResearch Centre, Oxford University) January 2019
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
#include <dirent.h> 
#include <regex.h>
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
    DIR *dirp;
    struct dirent *dir;
    regex_t regex;
    std::string IFSDATA_FILE, IC_ANCIL_FILE, CLIMATE_DATA_FILE, GRID_TYPE;
    int HORIZ_RESOLUTION;

    // Defaults to input arguments
    std::string OIFS_EXPID;           // model experiment id, must match string in filenames
    int NTHREADS=1;                   // default number ofexi OPENMP threads
    std::string NAMELIST="fort.4";    // NAMELIST file, this name is fixed

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
    options.direct_process_action = false;  // monitor won't get suspended/killed by BOINC
    options.send_status_msgs = false;

    retval = boinc_init_options(&options);
    if (retval) {
       fprintf(stderr, "BOINC init options failed!\n");
    }

    // Get the project path
    std::string project_path = dataBOINC.project_dir + std::string("/");
    fprintf(stderr,"current project directory is: %s\n", project_path.c_str());

    // Get the slots path (the current working path)
    char slot_path[_MAX_PATH];
    if (getcwd(slot_path, sizeof(slot_path)) == NULL)
      fprintf(stderr,"getcwd() returned an error\n");
    else
      fprintf(stderr,"current working directory is: %s\n", slot_path);

    fprintf(stderr, "(argv0) %s\n", argv[0]);
    fprintf(stderr, "(argv1) version: %s\n", argv[1]);
    fprintf(stderr, "(argv2) exptid: %s\n", argv[2]);
    fprintf(stderr, "(argv3) unique_member_id: %s\n", argv[3]);
    fprintf(stderr, "(argv4) batchid: %s\n", argv[4]);
    fprintf(stderr, "(argv5) wuid: %s\n", argv[5]);
    fprintf(stderr, "(argv6) fclen: %s\n", argv[6]);
    fprintf(stderr, "(argv7) start_date: %s\n", argv[7]);
    fflush(stderr);

    // Read the exptid, batchid, version, wuid from the command line
    std::string version = argv[1];
    std::string exptid = argv[2];
    std::string unique_member_id = argv[3];
    std::string batchid = argv[4];
    std::string wuid = argv[5];
    std::string fclen = argv[6];
    std::string start_date = argv[7];
    OIFS_EXPID = exptid;

    boinc_begin_critical_section();

    // Copy apps files to working directory
    std::string app_target = project_path + std::string("openifs_app_") + version + std::string(".zip");
    std::string app_destination = slot_path + std::string("/openifs_app_") + version + std::string(".zip");
    fprintf(stderr,"Copying: %s to: %s\n",app_target.c_str(),app_destination.c_str());
    boinc_copy(app_target.c_str(),app_destination.c_str());

    // Unzip the app zip file
    std::string app_zip = slot_path + std::string("/openifs_app_") + version + std::string(".zip");
    fprintf(stderr, "Unzipping app zip file: %s\n", app_zip.c_str());
    fflush(stderr);
    boinc_zip(UNZIP_IT,app_zip.c_str(),slot_path);


    // Copy namelist files to working directory
    std::string wu_target = project_path + std::string("openifs_wu_") + unique_member_id + std::string("_") + start_date +\
                      std::string("_") + fclen + std::string("_") + batchid + std::string("_") + wuid + std::string(".zip");
    std::string wu_destination = slot_path + std::string("/openifs_wu_") + unique_member_id + std::string("_") + start_date +\
                         std::string("_") + fclen + std::string("_") + batchid + std::string("_") + wuid + std::string(".zip");
    fprintf(stderr,"Copying namelist files from: %s to: %s\n",wu_target.c_str(),wu_destination.c_str());
    boinc_copy(wu_target.c_str(),wu_destination.c_str());

    // Unzip the namelist zip file
    std::string namelist_zip = slot_path + std::string("/openifs_wu_") + unique_member_id + std::string("_") + start_date +\
                      std::string("_") + fclen + std::string("_") + batchid + std::string("_") + wuid + std::string(".zip");
    fprintf(stderr, "Unzipping namelist zip file: %s\n", namelist_zip.c_str());
    fflush(stderr);
    boinc_zip(UNZIP_IT,namelist_zip.c_str(),slot_path);

    // Parse the fort.4 namelist for the filenames and variables
    std::string namelist_file = slot_path + std::string("/") + NAMELIST;
    const char strSearch[5][22]={"!IFSDATA_FILE=","!IC_ANCIL_FILE=","!CLIMATE_DATA_FILE=","!HORIZ_RESOLUTION=","!GRID_TYPE="};
    char* strFind[5] = {NULL,NULL,NULL,NULL,NULL};
    char strCpy[5][_MAX_PATH];
    char strTmp[_MAX_PATH];
    memset(strCpy,0x00,5*_MAX_PATH);
    memset(strTmp,0x00,_MAX_PATH);
    FILE* fParse = boinc_fopen(namelist_file.c_str(),"r");

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
            if (strFind[0]&&strFind[1]&&strFind[2]&&strFind[3]&&strFind[4]) {
                break;
            }
       }
       // Either feof or we hit the string		
       if (strCpy[0][0] != 0x00) {
            memset(strTmp, 0x00, _MAX_PATH);
            strncpy(strTmp, (char*)(strCpy[0] + strlen(strSearch[0])), 100);
            IFSDATA_FILE = strTmp;
            while(!IFSDATA_FILE.empty() && std::isspace(*IFSDATA_FILE.rbegin())) IFSDATA_FILE.erase(IFSDATA_FILE.length()-1);
            fprintf(stderr, "IFSDATA_FILE: %s\n", IFSDATA_FILE.c_str());
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
            HORIZ_RESOLUTION=atoi(strCpy[3] + strlen(strSearch[3]));
            fprintf(stderr, "HORIZ_RESOLUTION: %i\n", HORIZ_RESOLUTION);
       }
       if (strCpy[4][0] != 0x00) {
            memset(strTmp, 0x00, _MAX_PATH);
            strncpy(strTmp, (char*)(strCpy[4] + strlen(strSearch[4])), 100);
            GRID_TYPE = strTmp; 
            while(!GRID_TYPE.empty() && std::isspace(*GRID_TYPE.rbegin())) GRID_TYPE.erase(GRID_TYPE.length()-1);
            fprintf(stderr, "GRID_TYPE: %s\n", GRID_TYPE.c_str());
       }
       fclose(fParse);
    }


    // Copy the IC ancils to working directory
    std::string ic_ancil_target = project_path + IC_ANCIL_FILE + std::string(".zip");
    std::string ic_ancil_destination = slot_path + std::string("/") + IC_ANCIL_FILE + std::string(".zip");
    fprintf(stderr,"Copying IC ancils from: %s to: %s\n",ic_ancil_target.c_str(),ic_ancil_destination.c_str());
    boinc_copy(ic_ancil_target.c_str(),ic_ancil_destination.c_str());

    // Unzip the IC ancils zip file
    std::string ic_ancil_zip = slot_path + std::string("/") + IC_ANCIL_FILE + std::string(".zip");
    fprintf(stderr, "Unzipping IC ancils zip file: %s\n", ic_ancil_zip.c_str());
    fflush(stderr);
    boinc_zip(UNZIP_IT,ic_ancil_zip.c_str(),slot_path);


    // Make the ifsdata directory
    std::string ifsdata_folder = slot_path + std::string("/ifsdata");
    if (mkdir(ifsdata_folder.c_str(),S_IRWXU|S_IRWXG|S_IROTH|S_IXOTH) != 0) fprintf(stderr, "mkdir() ifsdata failed\n");

    // Copy the IFSDATA_FILE to working directory
    std::string ifsdata_target = project_path + IFSDATA_FILE + std::string(".zip");
    std::string ifsdata_destination = slot_path + std::string("/ifsdata/") + IFSDATA_FILE + std::string(".zip");
    fprintf(stderr,"Copying IFSDATA_FILE from: %s to: %s\n",ifsdata_target.c_str(),ifsdata_destination.c_str());
    boinc_copy(ifsdata_target.c_str(),ifsdata_destination.c_str());

    // Unzip the IFSDATA_FILE zip file
    std::string ifsdata_zip = slot_path + std::string("/ifsdata/") + IFSDATA_FILE + std::string(".zip");
    fprintf(stderr, "Unzipping IFSDATA_FILE zip file: %s\n", ifsdata_zip.c_str());
    fflush(stderr);
    boinc_zip(UNZIP_IT,ifsdata_zip.c_str(),slot_path+std::string("/ifsdata/"));


    // Make the climate data directory
    std::string climate_data_path = slot_path + std::string("/") + \
                       std::to_string(HORIZ_RESOLUTION) + std::string(GRID_TYPE);
    if (mkdir(climate_data_path.c_str(),S_IRWXU|S_IRWXG|S_IROTH|S_IXOTH) != 0) fprintf(stderr, "mkdir() climate data failed\n");

    // Copy the climate data file to working directory
    std::string climate_data_target = project_path + CLIMATE_DATA_FILE + std::string(".zip");
    std::string climate_data_destination = slot_path + std::string("/") + \
                                           std::to_string(HORIZ_RESOLUTION) + std::string(GRID_TYPE) + \
                                           std::string("/") + CLIMATE_DATA_FILE + std::string(".zip");
    fprintf(stderr,"Copying climate data file from: %s to: %s\n",climate_data_target.c_str(),climate_data_destination.c_str());
    boinc_copy(climate_data_target.c_str(),climate_data_destination.c_str());

    // Unzip the climate data zip file
    std::string climate_zip = slot_path + std::string("/") + \
                              std::to_string(HORIZ_RESOLUTION) + std::string(GRID_TYPE) + \
                              std::string("/") + CLIMATE_DATA_FILE + std::string(".zip");
    fprintf(stderr, "Unzipping the climate data zip file: %s\n", climate_zip.c_str());
    fflush(stderr);
    boinc_zip(UNZIP_IT,climate_zip.c_str(),slot_path+std::string("/")+std::to_string(HORIZ_RESOLUTION)+std::string(GRID_TYPE));


    char *pathvar;
    // Set the GRIB_SAMPLES_PATH environmental variable
    std::string GRIB_SAMPLES_var = std::string("GRIB_SAMPLES_PATH=") + slot_path + \
                                   std::string("/eccodes/ifs_samples/grib1_mlgrib2");
    if (putenv((char *)GRIB_SAMPLES_var.c_str())) {
      fprintf(stderr, "putenv failed \n");
      return 1;
    }
    pathvar = getenv("GRIB_SAMPLES_PATH");
    fprintf(stderr, "The current GRIB_SAMPLES_PATH is: %s\n", pathvar);

    // Set the GRIB_DEFINITION_PATH environmental variable
    std::string GRIB_DEF_var = std::string("GRIB_DEFINITION_PATH=") + slot_path + \
                               std::string("/eccodes/definitions");
    if (putenv((char *)GRIB_DEF_var.c_str())) {
      fprintf(stderr, "putenv failed \n");
      return 1;
    }
    pathvar = getenv("GRIB_DEFINITION_PATH");
    fprintf(stderr, "The current GRIB_DEFINITION_PATH is: %s\n", pathvar);

    // Set the OIFS_DUMMY_ACTION environmental variable, this controls what OpenIFS does if it goes into a dummy subroutine
    // Possible values are: 'quiet', 'verbose' or 'abort'
    std::string OIFS_var = std::string("OIFS_DUMMY_ACTION=abort");
    if (putenv((char *)OIFS_var.c_str())) {
      fprintf(stderr, "putenv failed \n");
      return 1;
    }
    pathvar = getenv("OIFS_DUMMY_ACTION");
    fprintf(stderr, "The current OIFS_DUMMY_ACTION is: %s\n", pathvar);

    // Set the OMP_NUM_THREADS environmental variable, the number of threads
    std::string OMP_NUM_var = std::string("OMP_NUM_THREADS=") + std::to_string(NTHREADS);
    if (putenv((char *)OMP_NUM_var.c_str())) {
      fprintf(stderr, "putenv failed \n");
      return 1;
    }
    pathvar = getenv("OMP_NUM_THREADS");
    fprintf(stderr, "The current OMP_NUM_THREADS is: %s\n", pathvar);

    // Set the OMP_SCHEDULE environmental variable, this enforces static thread scheduling
    std::string OMP_SCHED_var = std::string("OMP_SCHEDULE=STATIC");
    if (putenv((char *)OMP_SCHED_var.c_str())) {
      fprintf(stderr, "putenv failed \n");
      return 1;
    }
    pathvar = getenv("OMP_SCHEDULE");
    fprintf(stderr, "The current OMP_SCHEDULE is: %s\n", pathvar);

    // Set the DR_HOOK environmental variable, this controls the tracing facility in OpenIFS, off=0 and on=1
    std::string DR_HOOK_var = std::string("DR_HOOK=1");
    if (putenv((char *)DR_HOOK_var.c_str())) {
      fprintf(stderr, "putenv failed \n");
      return 1;
    }
    pathvar = getenv("DR_HOOK");
    fprintf(stderr, "The current DR_HOOK is: %s\n", pathvar);

    // Set the DR_HOOK_HEAPCHECK environmental variable, this ensures the heap size statistics are reported
    std::string DR_HOOK_HEAP_var = std::string("DR_HOOK_HEAPCHECK=no");
    if (putenv((char *)DR_HOOK_HEAP_var.c_str())) {
      fprintf(stderr, "putenv failed \n");
      return 1;
    }
    pathvar = getenv("DR_HOOK_HEAPCHECK");
    fprintf(stderr, "The current DR_HOOK_HEAPCHECK is: %s\n", pathvar);

    // Set the DR_HOOK_STACKCHECK environmental variable, this ensures the stack size statistics are reported
    std::string DR_HOOK_STACK_var = std::string("DR_HOOK_STACKCHECK=no");
    if (putenv((char *)DR_HOOK_STACK_var.c_str())) {
      fprintf(stderr, "putenv failed \n");
      return 1;
    }
    pathvar = getenv("DR_HOOK_STACKCHECK");
    fprintf(stderr, "The current DR_HOOK_STACKCHECK is: %s\n", pathvar);

    // Set the OMP_STACKSIZE environmental variable, OpenIFS needs more stack memory per process
    std::string OMP_STACK_var = std::string("OMP_STACKSIZE=128M");
    if (putenv((char *)OMP_STACK_var.c_str())) {
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
       std::string namelist_target = namelist_file;
       std::string namelist_destination = slot_path + std::string("/fort.4");
       fprintf(stderr,"Copying namelist file from: %s to: %s\n",namelist_target.c_str(),namelist_destination.c_str());
       boinc_copy(namelist_target.c_str(),namelist_destination.c_str());
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

    // Start the OpenIFS job
    std::string openifs_start = std::string("./master.exe -e ") + exptid;
    fprintf(stderr, "Starting the executable: %s\n", openifs_start.c_str());
    fflush(stderr);
    system(openifs_start.c_str());

    sleep_until(system_clock::now() + seconds(5));

    // Make the results folder
    std::string result_name = std::string("openifs_") + unique_member_id + std::string("_") + start_date + \
                              std::string("_") + fclen + std::string("_") + batchid + std::string("_") + wuid;

    boinc_end_critical_section();

    sleep_until(system_clock::now() + seconds(20));

    // Compile results zip file using BOINC zip
    ZipFileList zfl;
    zfl.clear();
    std::string node_file = slot_path + std::string("/NODE.001_01");
    zfl.push_back(node_file);
    std::string ifsstat_file = slot_path + std::string("/ifs.stat");
    zfl.push_back(ifsstat_file);

    // Read the list of files from the slots directory and add matching files to the zip
    dirp = opendir(slot_path);
    if (dirp) {
        while ((dir = readdir(dirp)) != NULL) {
          fprintf(stderr,"In slots folder: %s\n",dir->d_name);
          regcomp(&regex,"^[ICM+]",0);
          regcomp(&regex,"\\+",0);

          if (!regexec(&regex,dir->d_name,(size_t) 0,NULL,0)) {
            zfl.push_back(slot_path+std::string("/")+dir->d_name);
            fprintf(stderr,"Adding to the zip: %s\n",(slot_path+std::string("/")+dir->d_name).c_str());
          }
        }
        closedir(dirp);
    }

    if (zfl.size() > 0){
       std::string strOut = project_path + std::string("/") + result_name + std::string(".zip");
       boinc_zip(ZIP_IT, strOut.c_str(), &zfl);
    }

    fprintf(stderr, "Starting upload of result\n");
    fflush(stderr);

    // Upload the results file
    std::string upload_results = project_path + result_name + std::string(".zip");
    fprintf(stderr, "File being uploaded: %s\n", upload_results.c_str());
    fflush(stderr);
    boinc_upload_file(upload_results);

    fprintf(stderr, "Finished upload of result\n");
    fflush(stderr);

    sleep_until(system_clock::now() + seconds(20));

    boinc_finish(0);
    return 0;
}
