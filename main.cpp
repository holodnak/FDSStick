#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include "device.h"
#include "spi.h"
#include "fds.h"
#include "firmware.h"
        
void app_exit(int exitcode) {
    dev_close();
    exit(exitcode);
};

void help() {
    printf(
        "\n"
        "    -f file.fds [1..8]          write to flash (disk slot# 1..8)\n"
        "    -s file.fds [1..8]          read from flash\n"
        "    -r file.fds                 read disk\n"
        "    -R file.raw [file.bin]      read disk (raw)\n"
        "    -w file.fds                 write disk\n"
        "    -l                          list flash contents\n"
        "    -e [1..8 | all]             erase flash\n"
        //"    -D file [addr] [size]       dump flash\n"
        //"    -W file [addr]              write flash\n"
    );
    app_exit(1);
}

int main(int argc, char** argv) {
    setbuf(stdout,NULL);
    printf("FDSStick console app (" __DATE__ ")\n");

    if(!dev_open() || argc<2 || argv[1][0]!='-') {
        help();
    }
/*
    if(!firmware_update())  //auto-update old firmware
        app_exit(1);
*/
    bool success=false;
    switch(argv[1][1]) {

    case 'f': //flash -f file.fds [slot]
        if(argc<3)
            help();
        {
            int slot=1;
            if(argc>3)
                sscanf(argv[3],"%i",&slot);
            success=FDS_writeFlash(argv[2], slot);
        }
        break;

    case 's': //save -s file.fds [slot]
        if(argc<3)
            help();
        {
            int slot=1;
            if(argc>3)
                sscanf(argv[3],"%i",&slot);
            //TODO - name should be optional, it's already in flash
            success=FDS_readFlashToFDS(argv[2], slot);
        }
        break;

    case 'w':
        if(argc<3)
            help();
        success=FDS_writeDisk(argv[2]);
        break;

    case 'l':
        success=FDS_list();
        break;

    case 'r':   //readDisk -r file.fds
        if(argc<3)
            help();
        success=FDS_readDisk(NULL, NULL, argv[2]);
        break;

    case 'R':   //readRaw -R file.raw [file.bin]
        if(argc<3)
            help();
        success=FDS_readDisk(argv[2], argc>3?argv[3]:NULL, NULL);
        break;

    case 'e':   //erase -e [1..N | all]
        if(argc<3)
            help();
        {
            if(!strcmp(argv[2],"all")) {
                success=true;
                for(int addr=0; addr<dev_flashSize; addr+=SLOTSIZE)
                    success &= spi_erasePage(addr);
            } else {
                int slot=1;
                sscanf(argv[2],"%i",&slot);
                success=spi_erasePage(SLOTSIZE*(slot-1));
                //TODO - erase all slots of a game
            }
        }
        break;
/*
    case 'D':   //dump -D filename addr size
        if(argc<3)
            help();
        {
            int addr=0, size=dev_flashSize;
            if(argc>3)
                sscanf(argv[3],"%i",&addr);
            if(argc>4)
                sscanf(argv[4],"%i",&size);
            success=spi_dumpFlash(argv[2], addr, size);
            break;
        }

    case 'W':   //write -W file [addr]
        if(argc<3)
            help();
        {
            int addr=0;
            if(argc>3)
                sscanf(argv[3],"%i",&addr);
            success=spi_writeFile(argv[2], addr);
            break;
        }

    case 'u': //update -u filename
        if(argc<3)
            help();
        {
            if(spi_writeFile(argv[2], 0xff0000))
                success=dev_updateFirmware();
            break;
        }

    case 'T':   //mfgTest -T ...
        {
            dev_selfTest();
            success=true;
            break;
        }

*/
    default:
        help();
    }

    printf(success? "Ok.\n": "Failed.\n");
    if(!success)
        dev_printLastError();

    app_exit(success?0:1);
}
