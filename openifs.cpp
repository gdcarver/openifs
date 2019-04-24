//
// Control code for the OpenIFS application in the climateprediction.net project
//
// Written by Andy Bowery (Oxford eResearch Centre, Oxford University) February 2019
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
#include <sys/wait.h>
#include <string>
#include <sstream>
#include "./boinc/api/boinc_api.h"
#include "./boinc/zip/boinc_zip.h"

#ifndef _MAX_PATH
   #define _MAX_PATH 512
#endif

const char* stripPath(const char* path);
int checkChildStatus(long,int);
int checkBOINCStatus(long,int);
long launchProcess(const char*,const char*,const char*);

using namespace std::chrono;
using namespace std::this_thread;
using namespace std;

int main(int argc, char** argv) {
    std::string IFSDATA_FILE,IC_ANCIL_FILE,CLIMATE_DATA_FILE,GRID_TYPE,project_path,result_name,version;
    int HORIZ_RESOLUTION,process_running=0,retval=0;
    char* strFind[5] = {NULL,NULL,NULL,NULL,NULL};
    char strCpy[5][_MAX_PATH];
    char strTmp[_MAX_PATH];
    long handleProcess;
    struct dirent *dir;
    regex_t regex;
    char *pathvar;
    DIR *dirp;

    // Set defaults for input arguments
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
    options.handle_process_control = true;  // the control code will handle all suspend/quit/resume
    options.direct_process_action = false;  // the control won't get suspended/killed by BOINC
    options.send_status_msgs = false;

    retval = boinc_init_options(&options);
    if (retval) {
       fprintf(stderr,"..BOINC init options failed\n");
       return retval;
    }

    fprintf(stderr,"(argv0) %s\n",argv[0]);
    fprintf(stderr,"(argv1) start_date: %s\n",argv[1]);
    fprintf(stderr,"(argv2) exptid: %s\n",argv[2]);
    fprintf(stderr,"(argv3) unique_member_id: %s\n",argv[3]);
    fprintf(stderr,"(argv4) batchid: %s\n",argv[4]);
    fprintf(stderr,"(argv5) wuid: %s\n",argv[5]);
    fprintf(stderr,"(argv6) fclen: %s\n",argv[6]);
    fflush(stderr);

    // Read the exptid, batchid, version, wuid from the command line
    std::string start_date = argv[1];
    std::string exptid = argv[2];
    std::string unique_member_id = argv[3];
    std::string batchid = argv[4];
    std::string wuid = argv[5];
    std::string fclen = argv[6];
    OIFS_EXPID = exptid;

    // Get the slots path (the current working path)
    char slot_path[_MAX_PATH];
    if (getcwd(slot_path, sizeof(slot_path)) == NULL)
      fprintf(stderr,"..getcwd returned an error\n");
    else
      fprintf(stderr,"The current working directory is: %s\n",slot_path);

    if (!boinc_is_standalone()) {
      // Get the project path
      project_path = dataBOINC.project_dir + std::string("/");
      fprintf(stderr,"Current project directory is: %s\n",project_path.c_str());

      // Get the app version and re-parse to add a dot
      version = std::to_string(dataBOINC.app_version);
      if (version.length()==3) {
         version = version.insert(1,".");
         //fprintf(stderr,"version: %s\n",version.c_str());
      }
      else if (version.length()==4) {
         version = version.insert(2,".");
         //fprintf(stderr,"version: %s\n",version.c_str());
      }
      else {
         fprintf(stderr,"..Error with the length of app_version, length is: %lu\n",version.length());
         return 1;
      }

      // Obtain the name of the result for renaming the upload zip
      retval = boinc_resolve_filename("upload_file_1.zip",strTmp,_MAX_PATH);
      if (retval==0) {
         result_name = stripPath(strTmp);
         //fprintf(stderr,"strTmp: %s\n",strTmp);
         //fprintf(stderr,"result_name: %s\n",result_name.c_str());
      }
      else {
         fprintf(stderr, "..Failed to resolve result name\n");
         return retval;
      }

      fprintf(stderr,"The current version is: %s\n",version.c_str());
      fprintf(stderr,"The current result_name is: %s\n",result_name.c_str());
    }
    // Running in standalone
    else {
      fprintf(stderr,"Running in standalone mode\n");
      // Set the project path
      project_path = slot_path + std::string("/../projects/");
      fprintf(stderr,"Current project directory is: %s\n",project_path.c_str());
	    
      // Get the app version and result name
      version = argv[7];
      fprintf(stderr,"(argv7) app_version: %s\n",argv[7]);
      result_name = std::string("openifs_") + unique_member_id + std::string("_") + start_date + std::string("_") + \
                    fclen + std::string("_") + batchid + std::string("_") + wuid + std::string("_0") + std::string(".zip");
      fprintf(stderr,"The current result_name is: %s\n",result_name.c_str());
    }

    boinc_begin_critical_section();

    // Copy the app file to the working directory
    std::string app_target = project_path + std::string("openifs_app_") + version + std::string(".zip");
    std::string app_destination = slot_path + std::string("/openifs_app_") + version + std::string(".zip");
    fprintf(stderr,"Copying: %s to: %s\n",app_target.c_str(),app_destination.c_str());
    retval = boinc_copy(app_target.c_str(),app_destination.c_str());
    if (retval) {
       fprintf(stderr,"..Copying the app file to the working directory failed\n");
       return retval;
    }

    // Unzip the app zip file
    std::string app_zip = slot_path + std::string("/openifs_app_") + version + std::string(".zip");
    fprintf(stderr,"Unzipping the app zip file: %s\n",app_zip.c_str());
    fflush(stderr);
    retval = boinc_zip(UNZIP_IT,app_zip.c_str(),slot_path);
    if (retval) {
       fprintf(stderr,"..Unzipping the app file failed\n");
       return retval;
    }


    // Copy the namelist files to the working directory
    std::string wu_target = project_path + std::string("openifs_") + unique_member_id + std::string("_") + start_date +\
                      std::string("_") + fclen + std::string("_") + batchid + std::string("_") + wuid + std::string(".zip");
    std::string wu_destination = slot_path + std::string("/openifs_") + unique_member_id + std::string("_") + start_date +\
                         std::string("_") + fclen + std::string("_") + batchid + std::string("_") + wuid + std::string(".zip");
    fprintf(stderr,"Copying the namelist files from: %s to: %s\n",wu_target.c_str(),wu_destination.c_str());
    retval = boinc_copy(wu_target.c_str(),wu_destination.c_str());
    if (retval) {
       fprintf(stderr,"..Copying the namelist files to the working directory failed\n");
       return retval;
    }

    // Unzip the namelist zip file
    std::string namelist_zip = slot_path + std::string("/openifs_") + unique_member_id + std::string("_") + start_date +\
                      std::string("_") + fclen + std::string("_") + batchid + std::string("_") + wuid + std::string(".zip");
    fprintf(stderr,"Unzipping the namelist zip file: %s\n",namelist_zip.c_str());
    fflush(stderr);
    retval = boinc_zip(UNZIP_IT,namelist_zip.c_str(),slot_path);
    if (retval) {
       fprintf(stderr,"..Unzipping the namelist file failed\n");
       return retval;
    }

    // Parse the fort.4 namelist for the filenames and variables
    std::string namelist_file = slot_path + std::string("/") + NAMELIST;
    const char strSearch[5][22]={"!IFSDATA_FILE=","!IC_ANCIL_FILE=","!CLIMATE_DATA_FILE=","!HORIZ_RESOLUTION=","!GRID_TYPE="};
    memset(strCpy,0x00,5*_MAX_PATH);
    memset(strTmp,0x00,_MAX_PATH);
    FILE* fParse = boinc_fopen(namelist_file.c_str(),"r");

    // Read the namelist file
    if (!fParse) {
       fprintf(stderr,"..Opening the namelist file to read failed\n");
       return 1;
    }
    else {
       // Start at the top of the file
       fseek(fParse,0x00,SEEK_SET);
       memset(strTmp,0x00,_MAX_PATH);
       while (!feof(fParse)) {
           memset(strTmp,0x00,_MAX_PATH);
           fgets(strTmp,_MAX_PATH-1,fParse);
           if (!strFind[0]) {
               strFind[0] = strstr(strTmp,strSearch[0]);
               if (strFind[0]) {
                   strcpy(strCpy[0],strFind[0]);
               }
            }
            if (!strFind[1]) {
                strFind[1] = strstr(strTmp,strSearch[1]);
                if (strFind[1]) {
                    strcpy(strCpy[1],strFind[1]);
		}
            }
            if (!strFind[2]) {
                strFind[2] = strstr(strTmp,strSearch[2]);
                if (strFind[2]) {
                    strcpy(strCpy[2],strFind[2]);
                }
            }
            if (!strFind[3]) {
                strFind[3] = strstr(strTmp,strSearch[3]);
                if (strFind[3]) {
                    strcpy(strCpy[3],strFind[3]);
                }
            }
            if (!strFind[4]) {
                strFind[4] = strstr(strTmp,strSearch[4]);
                if (strFind[4]) {
                    strcpy(strCpy[4],strFind[4]);
                }
            }
            if (strFind[0]&&strFind[1]&&strFind[2]&&strFind[3]&&strFind[4]) {
                break;
            }
       }
       // Either feof or we hit the string		
       if (strCpy[0][0] != 0x00) {
            memset(strTmp,0x00,_MAX_PATH);
            strncpy(strTmp,(char*)(strCpy[0] + strlen(strSearch[0])),100);
            IFSDATA_FILE = strTmp;
            // Handle any white space in tags
            while(!IFSDATA_FILE.empty() && \
                  std::isspace(*IFSDATA_FILE.rbegin())) IFSDATA_FILE.erase(IFSDATA_FILE.length()-1);
            fprintf(stderr,"IFSDATA_FILE: %s\n",IFSDATA_FILE.c_str());
       }
       if (strCpy[1][0] != 0x00) {
            memset(strTmp,0x00,_MAX_PATH);
            strncpy(strTmp,(char*)(strCpy[1] + strlen(strSearch[1])),100);
            IC_ANCIL_FILE = strTmp; 
            while(!IC_ANCIL_FILE.empty() && \
                  std::isspace(*IC_ANCIL_FILE.rbegin())) IC_ANCIL_FILE.erase(IC_ANCIL_FILE.length()-1);
            fprintf(stderr,"IC_ANCIL_FILE: %s\n",IC_ANCIL_FILE.c_str());
       }
       if (strCpy[2][0] != 0x00) {
            memset(strTmp,0x00,_MAX_PATH);
            strncpy(strTmp,(char*)(strCpy[2] + strlen(strSearch[2])),100);
            CLIMATE_DATA_FILE = strTmp; 
            while(!CLIMATE_DATA_FILE.empty() && \
                  std::isspace(*CLIMATE_DATA_FILE.rbegin())) CLIMATE_DATA_FILE.erase(CLIMATE_DATA_FILE.length()-1);
            fprintf(stderr,"CLIMATE_DATA_FILE: %s\n",CLIMATE_DATA_FILE.c_str());
       }
       if (strCpy[3][0] != 0x00) {
            HORIZ_RESOLUTION=atoi(strCpy[3] + strlen(strSearch[3]));
            fprintf(stderr,"HORIZ_RESOLUTION: %i\n",HORIZ_RESOLUTION);
       }
       if (strCpy[4][0] != 0x00) {
            memset(strTmp,0x00,_MAX_PATH);
            strncpy(strTmp,(char*)(strCpy[4] + strlen(strSearch[4])),100);
            GRID_TYPE = strTmp; 
            while(!GRID_TYPE.empty() && std::isspace(*GRID_TYPE.rbegin())) GRID_TYPE.erase(GRID_TYPE.length()-1);
            fprintf(stderr,"GRID_TYPE: %s\n",GRID_TYPE.c_str());
       }
       fclose(fParse);
    }

    // Copy the IC ancils to working directory
    std::string ic_ancil_target = project_path + IC_ANCIL_FILE + std::string(".zip");
    std::string ic_ancil_destination = slot_path + std::string("/") + IC_ANCIL_FILE + std::string(".zip");
    fprintf(stderr,"Copying IC ancils from: %s to: %s\n",ic_ancil_target.c_str(),ic_ancil_destination.c_str());
    retval = boinc_copy(ic_ancil_target.c_str(),ic_ancil_destination.c_str());
    if (retval) {
       fprintf(stderr,"..Copying the IC ancils to the working directory failed\n");
       return retval;
    }

    // Unzip the IC ancils zip file
    std::string ic_ancil_zip = slot_path + std::string("/") + IC_ANCIL_FILE + std::string(".zip");
    fprintf(stderr,"Unzipping the IC ancils zip file: %s\n",ic_ancil_zip.c_str());
    fflush(stderr);
    retval = boinc_zip(UNZIP_IT,ic_ancil_zip.c_str(),slot_path);
    if (retval) {
       fprintf(stderr,"..Unzipping the IC ancils file failed\n");
       return retval;
    }


    // Make the ifsdata directory
    std::string ifsdata_folder = slot_path + std::string("/ifsdata");
    if (mkdir(ifsdata_folder.c_str(),S_IRWXU|S_IRWXG|S_IROTH|S_IXOTH) != 0) fprintf(stderr,"..mkdir for ifsdata folder failed\n");

    // Copy the IFSDATA_FILE to working directory
    std::string ifsdata_target = project_path + IFSDATA_FILE + std::string(".zip");
    std::string ifsdata_destination = slot_path + std::string("/ifsdata/") + IFSDATA_FILE + std::string(".zip");
    fprintf(stderr,"Copying IFSDATA_FILE from: %s to: %s\n",ifsdata_target.c_str(),ifsdata_destination.c_str());
    retval = boinc_copy(ifsdata_target.c_str(),ifsdata_destination.c_str());
    if (retval) {
       fprintf(stderr,"..Copying the IFSDATA file to the working directory failed\n");
       return retval;
    }

    // Unzip the IFSDATA_FILE zip file
    std::string ifsdata_zip = slot_path + std::string("/ifsdata/") + IFSDATA_FILE + std::string(".zip");
    fprintf(stderr,"Unzipping IFSDATA_FILE zip file: %s\n", ifsdata_zip.c_str());
    fflush(stderr);
    retval = boinc_zip(UNZIP_IT,ifsdata_zip.c_str(),slot_path+std::string("/ifsdata/"));
    if (retval) {
       fprintf(stderr,"..Unzipping the IFSDATA file failed\n");
       return retval;
    }


    // Make the climate data directory
    std::string climate_data_path = slot_path + std::string("/") + \
                       std::to_string(HORIZ_RESOLUTION) + std::string(GRID_TYPE);
    if (mkdir(climate_data_path.c_str(),S_IRWXU|S_IRWXG|S_IROTH|S_IXOTH) != 0) \
                       fprintf(stderr,"..mkdir for the climate data folder failed\n");

    // Copy the climate data file to working directory
    std::string climate_data_target = project_path + CLIMATE_DATA_FILE + std::string(".zip");
    std::string climate_data_destination = slot_path + std::string("/") + \
                                           std::to_string(HORIZ_RESOLUTION) + std::string(GRID_TYPE) + \
                                           std::string("/") + CLIMATE_DATA_FILE + std::string(".zip");
    fprintf(stderr,"Copying the climate data file from: %s to: %s\n",climate_data_target.c_str(),climate_data_destination.c_str());
    retval = boinc_copy(climate_data_target.c_str(),climate_data_destination.c_str());
    if (retval) {
       fprintf(stderr,"..Copying the climate data file to the working directory failed\n");
       return retval;
    }

    // Unzip the climate data zip file
    std::string climate_zip = slot_path + std::string("/") + \
                              std::to_string(HORIZ_RESOLUTION) + std::string(GRID_TYPE) + \
                              std::string("/") + CLIMATE_DATA_FILE + std::string(".zip");
    fprintf(stderr,"Unzipping the climate data zip file: %s\n",climate_zip.c_str());
    fflush(stderr);
    retval = boinc_zip(UNZIP_IT,climate_zip.c_str(),\
                       slot_path+std::string("/")+std::to_string(HORIZ_RESOLUTION)+std::string(GRID_TYPE));
    if (retval) {
       fprintf(stderr,"..Unzipping the climate data file failed\n");
       return retval;
    }


    // Set the OIFS_DUMMY_ACTION environmental variable, this controls what OpenIFS does if it goes into a dummy subroutine
    // Possible values are: 'quiet', 'verbose' or 'abort'
    std::string OIFS_var = std::string("OIFS_DUMMY_ACTION=abort");
    if (putenv((char *)OIFS_var.c_str())) {
      fprintf(stderr,"..Setting the OIFS_DUMMY_ACTION environmental variable failed\n");
      return 1;
    }
    pathvar = getenv("OIFS_DUMMY_ACTION");
    fprintf(stderr,"The OIFS_DUMMY_ACTION environmental variable is: %s\n",pathvar);

    // Set the OMP_NUM_THREADS environmental variable, the number of threads
    std::string OMP_NUM_var = std::string("OMP_NUM_THREADS=") + std::to_string(NTHREADS);
    if (putenv((char *)OMP_NUM_var.c_str())) {
      fprintf(stderr,"..Setting the OMP_NUM_THREADS environmental variable failed\n");
      return 1;
    }
    pathvar = getenv("OMP_NUM_THREADS");
    fprintf(stderr,"The OMP_NUM_THREADS environmental variable is: %s\n",pathvar);

    // Set the OMP_SCHEDULE environmental variable, this enforces static thread scheduling
    std::string OMP_SCHED_var = std::string("OMP_SCHEDULE=STATIC");
    if (putenv((char *)OMP_SCHED_var.c_str())) {
      fprintf(stderr,"..Setting the OMP_SCHEDULE environmental variable failed\n");
      return 1;
    }
    pathvar = getenv("OMP_SCHEDULE");
    fprintf(stderr,"The OMP_SCHEDULE environmental variable is: %s\n",pathvar);

    // Set the DR_HOOK environmental variable, this controls the tracing facility in OpenIFS, off=0 and on=1
    std::string DR_HOOK_var = std::string("DR_HOOK=1");
    if (putenv((char *)DR_HOOK_var.c_str())) {
      fprintf(stderr,"..Setting the DR_HOOK environmental variable failed\n");
      return 1;
    }
    pathvar = getenv("DR_HOOK");
    fprintf(stderr,"The DR_HOOK environmental variable is: %s\n",pathvar);

    // Set the DR_HOOK_HEAPCHECK environmental variable, this ensures the heap size statistics are reported
    std::string DR_HOOK_HEAP_var = std::string("DR_HOOK_HEAPCHECK=no");
    if (putenv((char *)DR_HOOK_HEAP_var.c_str())) {
      fprintf(stderr,"..Setting the DR_HOOK_HEAPCHECK environmental variable failed\n");
      return 1;
    }
    pathvar = getenv("DR_HOOK_HEAPCHECK");
    fprintf(stderr,"The DR_HOOK_HEAPCHECK environmental variable is: %s\n",pathvar);

    // Set the DR_HOOK_STACKCHECK environmental variable, this ensures the stack size statistics are reported
    std::string DR_HOOK_STACK_var = std::string("DR_HOOK_STACKCHECK=no");
    if (putenv((char *)DR_HOOK_STACK_var.c_str())) {
      fprintf(stderr,"..Setting the DR_HOOK_STACKCHECK environmental variable failed\n");
      return 1;
    }
    pathvar = getenv("DR_HOOK_STACKCHECK");
    fprintf(stderr, "The DR_HOOK_STACKCHECK environmental variable is: %s\n",pathvar);

    // Set the OMP_STACKSIZE environmental variable, OpenIFS needs more stack memory per process
    std::string OMP_STACK_var = std::string("OMP_STACKSIZE=128M");
    if (putenv((char *)OMP_STACK_var.c_str())) {
      fprintf(stderr,"..Setting the OMP_STACKSIZE environmental variable failed\n");
      return 1;
    }
    pathvar = getenv("OMP_STACKSIZE");
    fprintf(stderr,"The OMP_STACKSIZE environmental variable is: %s\n",pathvar);

    // Check for the existence of the namelist
    struct stat buffer;
    if(NAMELIST != "fort.4") {
       fprintf(stderr,"The namelist file path is: %s\n",namelist_file.c_str());
       fflush(stderr);
       if (stat((char *)namelist_file.c_str(),&buffer) < 0){
          fprintf(stderr,"..The namelist file %s does not exist\n",NAMELIST.c_str());
       }
       // Rename the namelist file to fort.4
       std::string namelist_target = namelist_file;
       std::string namelist_destination = slot_path + std::string("/fort.4");
       fprintf(stderr,"Renaming the namelist file from: %s to: %s\n",namelist_target.c_str(),namelist_destination.c_str());
       retval = boinc_copy(namelist_target.c_str(),namelist_destination.c_str());
       if (retval) {
          fprintf(stderr,"..Renaming the namelist file failed\n");
          return retval;
       }
    }
    if (stat((char *)namelist_file.c_str(), &buffer) < 0) {
       fprintf(stderr,"..The namelist file %s does not exist\n",NAMELIST.c_str());
    }

    // Set the core dump size to 0
    struct rlimit core_limits;
    core_limits.rlim_cur = core_limits.rlim_max = 0;
    if (setrlimit(RLIMIT_CORE, &core_limits) != 0) fprintf(stderr,"..Setting the core dump size to 0 failed\n");

    // Set the stack limit to be unlimited
    struct rlimit stack_limits;
    stack_limits.rlim_cur = stack_limits.rlim_max = RLIM_INFINITY;
    if (setrlimit(RLIMIT_STACK, &stack_limits) != 0) fprintf(stderr,"..Setting the stack limit to unlimited failed\n");


    // Start the OpenIFS job
    std::string strCmd = slot_path + std::string("/./master.exe");
    handleProcess = launchProcess(slot_path,strCmd.c_str(),exptid.c_str());
    if (handleProcess > 0) process_running = 1;

    boinc_end_critical_section();

    // Periodically check the process status and the BOINC client status
    while (process_running > 0) {
       sleep_until(system_clock::now() + seconds(1));
       process_running = checkChildStatus(handleProcess,process_running);
       process_running = checkBOINCStatus(handleProcess,process_running);
    }

    boinc_begin_critical_section();

    // Compile results zip file using BOINC zip
    ZipFileList zfl;
    zfl.clear();
    std::string node_file = slot_path + std::string("/NODE.001_01");
    zfl.push_back(node_file);
    std::string ifsstat_file = slot_path + std::string("/ifs.stat");
    zfl.push_back(ifsstat_file);

    // Read the list of files from the slots directory and add the matching files to the list of files for the zip
    dirp = opendir(slot_path);
    if (dirp) {
        while ((dir = readdir(dirp)) != NULL) {
          //fprintf(stderr,"In slots folder: %s\n",dir->d_name);
          regcomp(&regex,"^[ICM+]",0);
          regcomp(&regex,"\\+",0);

          if (!regexec(&regex,dir->d_name,(size_t) 0,NULL,0)) {
            zfl.push_back(slot_path+std::string("/")+dir->d_name);
            fprintf(stderr,"Adding to the zip: %s\n",(slot_path+std::string("/")+dir->d_name).c_str());
          }
        }
        closedir(dirp);
    }

    // Create the zipped upload file from the list of files added to zfl
    std::string upload_results = project_path + result_name;
    if (zfl.size() > 0){
       retval = boinc_zip(ZIP_IT,upload_results.c_str(),&zfl);
       if (retval) {
          fprintf(stderr,"..Creating the zipped upload file failed\n");
          return retval;
       }
    }

    // Upload the result file
    fprintf(stderr,"Starting the upload of the result file\n");
    fprintf(stderr,"File being uploaded: %s\n",upload_results.c_str());
    fflush(stderr);
    boinc_upload_file(upload_results);
    fprintf(stderr,"Finished the upload of the result file\n");
    fflush(stderr);

    sleep_until(system_clock::now() + seconds(20));

    boinc_end_critical_section();

    boinc_finish(0);
    return 0;
}


const char* stripPath(const char* path){
        int jj;
        for (jj = (int) strlen(path);
        jj > 0 && path[jj-1] != '/' && path[jj-1] != '\\'; jj--);
        return (const char*) path+jj;
}


int checkChildStatus(long handleProcess, int process_running) {
    int stat;
    //fprintf(stderr,"waitpid: %i\n",waitpid(handleProcess,0,WNOHANG));

    // Check whether child processed has exited
    if (waitpid(handleProcess,&stat,WNOHANG)==-1) {
       process_running = 0;
       // Child exited normally
       if (WIFEXITED(stat)) {
          fprintf(stderr,"The child process terminated with status: %d\n",WEXITSTATUS(stat));
          fflush(stderr);
       }
       // Child process has exited
       else if (WIFSIGNALED(stat)) {
          fprintf(stderr,"..The child process has been killed with signal: %d\n",WTERMSIG(stat));
          fflush(stderr);
       }
       // Child is stopped
       else if (WIFSTOPPED(stat)) {
          fprintf(stderr,"..The child process has stopped with signal: %d\n",WSTOPSIG(stat));
          fflush(stderr);
       }
    }
    return process_running;
}


int checkBOINCStatus(long handleProcess, int process_running) {
    BOINC_STATUS status;
    boinc_get_status(&status);

    // If a quit, abort or no heartbeat has been received from the BOINC client, end child process
    if (status.quit_request) {
       fprintf(stderr,"Quit request received from BOINC client, ending the child process\n");
       fflush(stderr);
       kill(handleProcess,SIGKILL);
       process_running = 0;
       return process_running;
    }
    else if (status.abort_request) {
       fprintf(stderr,"Abort request received from BOINC client, ending the child process\n");
       fflush(stderr);
       kill(handleProcess,SIGKILL);
       process_running = 0;
       return process_running;
    }
    else if (status.no_heartbeat) {
       fprintf(stderr,"No heartbeat received from BOINC client, ending the child process\n");
       fflush(stderr);
       kill(handleProcess,SIGKILL);
       process_running = 0;
       return process_running;
    }
    // Else if BOINC client is suspended, suspend child process and periodically check BOINC client status
    else {
       if (status.suspended) {
          fprintf(stderr,"Suspend request received from the BOINC client, suspending the child process\n");
          fflush(stderr);
          kill(handleProcess,SIGSTOP);

          while (status.suspended) {
             boinc_get_status(&status);
             if (status.quit_request) {
                fprintf(stderr,"Quit request received from the BOINC client, ending the child process\n");
                fflush(stderr);
                kill(handleProcess,SIGKILL);
                process_running = 0;
                return process_running;
             }
             else if (status.abort_request) {
                fprintf(stderr,"Abort request received from the BOINC client, ending the child process\n");
                fflush(stderr);
                kill(handleProcess,SIGKILL);
                process_running = 0;
                return process_running;
             }
             else if (status.no_heartbeat) {
                fprintf(stderr,"No heartbeat received from the BOINC client, ending the child process\n");
                fflush(stderr);
                kill(handleProcess,SIGKILL);
                process_running = 0;
                return process_running;
             }
             sleep_until(system_clock::now() + seconds(1));
          }
          // Resume child process
          fprintf(stderr,"Resuming the child process\n");
          fflush(stderr);
          kill(handleProcess,SIGCONT);
          process_running = 1;
       }
       return process_running;
    }
}


long launchProcess(const char* slot_path,const char* strCmd,const char* exptid)
{
    int retval = 0;
    long handleProcess;

    fprintf(stderr,"slot_path: %s\n",slot_path);
    fprintf(stderr,"strCmd: %s\n",strCmd);
    fprintf(stderr,"exptid: %s\n",exptid);
    fflush(stderr);

    switch((handleProcess=fork())) {
       case -1: {
          fprintf(stderr,"..Unable to start a new child process\n");
          exit(0);
          break;
       }
       case 0: { //The child process
          char *pathvar;
          // Set the GRIB_SAMPLES_PATH environmental variable
          std::string GRIB_SAMPLES_var = std::string("GRIB_SAMPLES_PATH=") + slot_path + \
                                         std::string("/eccodes/ifs_samples/grib1_mlgrib2");
          if (putenv((char *)GRIB_SAMPLES_var.c_str())) {
            fprintf(stderr,"..Setting the GRIB_SAMPLES_PATH failed\n");
          }
          pathvar = getenv("GRIB_SAMPLES_PATH");
          fprintf(stderr,"The GRIB_SAMPLES_PATH environmental variable is: %s\n",pathvar);

          // Set the GRIB_DEFINITION_PATH environmental variable
          std::string GRIB_DEF_var = std::string("GRIB_DEFINITION_PATH=") + slot_path + \
                                     std::string("/eccodes/definitions");
          if (putenv((char *)GRIB_DEF_var.c_str())) {
            fprintf(stderr,"..Setting the GRIB_DEFINITION_PATH failed\n");
          }
          pathvar = getenv("GRIB_DEFINITION_PATH");
          fprintf(stderr,"The GRIB_DEFINITION_PATH environmental variable is: %s\n",pathvar);

          fprintf(stderr,"Executing the command: %s\n",strCmd);   
          retval = execl(strCmd,strCmd,"-e",exptid,NULL);

          // If execl returns then there was an error
          fprintf(stderr,"..The execl(%s,%s,%s) command failed\n",slot_path,strCmd,exptid);
          fflush(stderr);
          exit(retval);
          break;
       }
       default: 
          fprintf(stderr,"The child process has been launched with process id: %ld\n",handleProcess);
          fflush(stderr);
    }
    return handleProcess;
}
