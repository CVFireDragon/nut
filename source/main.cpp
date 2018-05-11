#include <string.h>
#include <stdio.h>
#include <stdlib.h> 
#include <dirent.h>
#include <fstream>
#include <vector>

#include <switch.h>

const char * EXPORT_DIR = "save/";
const char * INJECT_DIR = "inject/";

Result getSaveList(std::vector<FsSaveDataInfo> & saveInfoList) {
    Result rc=0;
    FsSaveDataIterator iterator;
    size_t total_entries=0;
    FsSaveDataInfo info;

    rc = fsOpenSaveDataIterator(&iterator, FsSaveDataSpaceId_NandUser);//See libnx fs.h.
    if (R_FAILED(rc)) {
        printf("fsOpenSaveDataIterator() failed: 0x%x\n", rc);
        return rc;
    }

    rc = fsSaveDataIteratorRead(&iterator, &info, 1, &total_entries);//See libnx fs.h.
    if (R_FAILED(rc))
        return rc;
    if (total_entries==0)
        return MAKERESULT(Module_Libnx, LibnxError_NotFound);

    for(; R_SUCCEEDED(rc) && total_entries > 0; 
        rc = fsSaveDataIteratorRead(&iterator, &info, 1, &total_entries)) {
        if (info.SaveDataType == FsSaveDataType_SaveData) {
            saveInfoList.push_back(info);
        }
    }

    fsSaveDataIteratorClose(&iterator);

    return 0;
}

Result mountSaveByTitleAndUser(u64 titleID, u128 userID) {
    Result rc=0;
    int ret=0;
    FsFileSystem tmpfs;

    printf("\n\nUsing titleID=0x%016lx userID: 0x%lx 0x%lx\n", titleID, (u64)(userID>>64), (u64)userID);

    rc = fsMount_SaveData(&tmpfs, titleID, userID);//See also libnx fs.h.
    if (R_FAILED(rc)) {
        printf("fsMount_SaveData() failed: 0x%x\n", rc);
        return rc;
    }

    ret = fsdevMountDevice("save", tmpfs);
    if (ret==-1) {
        printf("fsdevMountDevice() failed.\n");
        rc = ret;
        return rc;
    }

    return rc;
}

int isDirectory(const char *path) {
   struct stat statbuf;
   if (stat(path, &statbuf) != 0)
       return 0;
   return S_ISDIR(statbuf.st_mode);
}

int cpFile(const char * filenameI, const char * filenameO) {
    remove( filenameO );

    std::ifstream src(filenameI, std::ios::binary);
    std::ofstream dst(filenameO, std::ios::binary);

    dst << src.rdbuf();

    return 0;
}


int copyAllSave(const char * dev, const char * path, bool isInject) {
    DIR* dir;
    struct dirent* ent;
    char dirPath[0x100];
    if(isInject) {
        strcpy(dirPath, INJECT_DIR);
        strcat(dirPath, path);
    } else {                    
        strcpy(dirPath, dev);
        strcat(dirPath, path);
    }

    dir = opendir(dirPath);
    if(dir==NULL)
    {
        printf("Failed to open dir: %s\n", dirPath);
        return -1;
    }
    else
    {
        printf("Contents from %s:\n", dirPath);
        while ((ent = readdir(dir)))
        {

            char filename[0x100];
            strcpy(filename, path);
            strcat(filename, "/");
            strcat(filename, ent->d_name);

            char filenameI[0x100];
            char filenameO[0x100];
            if(isInject) {
                strcpy(filenameI, INJECT_DIR);
                strcat(filenameI, filename);

                strcpy(filenameO, dev);
                strcat(filenameO, filename);
            } else {
                strcpy(filenameI, dev);
                strcat(filenameI, filename);

                strcpy(filenameO, EXPORT_DIR);
                strcat(filenameO, filename);
            }

            printf("Copying %s...\n", filenameI);

            if(isDirectory(filenameI)) {
                mkdir(filenameO, 0700);
                int res = copyAllSave(dev, filename, isInject);
                if(res != 0)
                    return res;
            } else {
                cpFile(filenameI, filenameO);
            }
        }
        closedir(dir);
        printf("Finished %s.\n", dirPath);
        return 0;
    }
}

Result getTitleName(u64 titleID, char * name) {
    Result rc=0;

    NsApplicationControlData *buf=NULL;
    size_t outsize=0;

    NacpLanguageEntry *langentry = NULL;

    buf = (NsApplicationControlData*)malloc(sizeof(NsApplicationControlData));
    if (buf==NULL) {
        rc = MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);
        printf("Failed to alloc mem.\n");
    }
    else {
        memset(buf, 0, sizeof(NsApplicationControlData));
    }

    if (R_SUCCEEDED(rc)) {
        rc = nsInitialize();
        if (R_FAILED(rc)) {
            printf("nsInitialize() failed: 0x%x\n", rc);
        }
    }

    if (R_SUCCEEDED(rc)) {
        rc = nsGetApplicationControlData(1, titleID, buf, sizeof(NsApplicationControlData), &outsize);
        if (R_FAILED(rc)) {
            printf("nsGetApplicationControlData() failed: 0x%x\n", rc);
        }

        if (outsize < sizeof(buf->nacp)) {
            rc = -1;
            printf("Outsize is too small: 0x%lx.\n", outsize);
        }

        if (R_SUCCEEDED(rc)) {
            rc = nacpGetLanguageEntry(&buf->nacp, &langentry);

            if (R_FAILED(rc) || langentry==NULL) printf("Failed to load LanguageEntry.\n");
        }

        if (R_SUCCEEDED(rc)) {
            memset(name, 0, sizeof(*name));
            strncpy(name, langentry->name, sizeof(langentry->name));//Don't assume the nacp string is NUL-terminated for safety.
        }

        nsExit();
    }

    return rc;
}

Result getUserNameById(u128 userID, char * username) {
    Result rc=0;

    AccountProfile profile;
    AccountUserData userdata;
    AccountProfileBase profilebase;

    memset(&userdata, 0, sizeof(userdata));
    memset(&profilebase, 0, sizeof(profilebase));

    rc = accountInitialize();
    if (R_FAILED(rc)) {
        printf("accountInitialize() failed: 0x%x\n", rc);
    }

    if (R_SUCCEEDED(rc)) {
        rc = accountGetProfile(&profile, userID);

        if (R_FAILED(rc)) {
            printf("accountGetProfile() failed: 0x%x\n", rc);
        }
        

        if (R_SUCCEEDED(rc)) {
            rc = accountProfileGet(&profile, &userdata, &profilebase);//userdata is otional, see libnx acc.h.

            if (R_FAILED(rc)) {
                printf("accountProfileGet() failed: 0x%x\n", rc);
            }

            if (R_SUCCEEDED(rc)) {
                memset(username,  0, sizeof(*username));
                strncpy(username, profilebase.username, sizeof(profilebase.username));//Even though profilebase.username usually has a NUL-terminator, don't assume it does for safety.
            }
            accountProfileClose(&profile);
        }
        accountExit();
    }

    return rc;
}

int selectSaveFromList(int & selection, int change,
    std::vector<FsSaveDataInfo> & saveInfoList, FsSaveDataInfo & info) {

    selection += change;
    change %= saveInfoList.size();
    if (selection < 0) {
        selection += saveInfoList.size();
    } else if (selection > 0 
        && static_cast<unsigned int>(selection) >= saveInfoList.size()) {
        selection -= saveInfoList.size();
    }

    info = saveInfoList.at(selection);
    char name[0x201];
    getTitleName(info.titleID, name);
    char username[0x21];
    getUserNameById(info.userID, username);
    printf("\r                                                                               ");
    gfxFlushBuffers();
    gfxSwapBuffers();
    gfxWaitForVsync();
    printf("\rSelected: %s \t User: %s", name, username);

    return selection;
}


int main(int argc, char **argv)
{

    Result rc=0;

    gfxInitDefault();
    consoleInit(NULL);

    std::vector<FsSaveDataInfo> saveInfoList;

    if (R_FAILED(getSaveList(saveInfoList))) {
        printf("Failed to get save list 0x%x\n", rc);
    }

    printf("Y'allAreNUTs v0.1\n");
    printf("Press A to dump save to 'save/'; Press X to inject contents from 'inject/'\n");
    printf("Press UP and DOWN to select a save; Press PLUS to quit\n\n");

    // Main loop
    int selection = -1;
    FsSaveDataInfo info;
    selectSaveFromList(selection, 1, saveInfoList, info);
    while(appletMainLoop())
    {
        //Scan all the inputs. This should be done once for each frame
        hidScanInput();

        //hidKeysDown returns information about which buttons have been just pressed (and they weren't in the previous frame)
        u64 kDown = hidKeysDown(CONTROLLER_P1_AUTO);

        if (kDown & KEY_UP) {
            selectSaveFromList(selection, -1, saveInfoList, info);
        }

        if (kDown & KEY_DOWN) {
            selectSaveFromList(selection, 1, saveInfoList, info);
        }

        if (kDown & KEY_A) {
            mountSaveByTitleAndUser(info.titleID, info.userID);
            mkdir(EXPORT_DIR, 0700);
            copyAllSave("save:/", ".", false);
            printf("Dump over.\n\n");
            fsdevUnmountDevice("save");
        }

        if (kDown & KEY_X) {
            mountSaveByTitleAndUser(info.titleID, info.userID);
            if( copyAllSave("save:/", ".", true) == 0 ) {
                rc = fsdevCommitDevice("save");
                if (R_SUCCEEDED(rc)) {
                    printf("Changes committed.\n\n");
                } else {
                    printf("fsdevCommitDevice() failed: %x\n", rc);
                    printf("Try injecting less data maybe?\n\n");
                }
            }
            fsdevUnmountDevice("save");
        }

        if (kDown & KEY_PLUS) {
            break; // break in order to return to hbmenu
        }

        gfxFlushBuffers();
        gfxSwapBuffers();
        gfxWaitForVsync();
    }

    gfxExit();
    return 0;
}
