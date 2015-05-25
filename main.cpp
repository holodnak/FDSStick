#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include "device.h"
#include "spi.h"
#include "fds.h"
        
void app_exit(int exitcode) {
    dev_close();
    exit(exitcode);
};

void help() {
    printf(
        "\n"
        //"    -D file [addr] [size]       dump SPI flash\n"
        //"    -W file [addr]              write SPI flash\n"
        "    -f file.fds [1..8]          write to flash (disk slot# 1..8)\n"
        "    -l                          list files\n"
        "    -r file.fds                 read disk\n"
        "    -R file.raw                 read disk (raw)\n"
        "    -w file.fds                 write disk\n"
        "    -u file.fw                  update firmware\n"
        //"    -R                          reset\n"
    );
    app_exit(1);
}

int main(int argc, char** argv) {
    setbuf(stdout,NULL);
    printf("FDSStick console app (" __DATE__ ")\n");

    if(!dev_open() || argc<2 || argv[1][0]!='-') {
        help();
    }

    bool success=false;
    switch(argv[1][1]) {

    case 'u': //update -u filename
        if(argc<3)
            help();
        {
            if(spi_writeFile(argv[2], 0xff0000))
                success=dev_updateFirmware();
            break;
        }

    case 'f': //flash -f file.fds [slot]
        if(argc<3)
            help();
        {
            int slot=1;
            if(argc>3)
                sscanf(argv[3],"%i",&slot);
            if(--slot > 7)
                slot=0;
            success=FDS_writeFlash(argv[2], slot);
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

    case 'r':   //readDisk -r file
        if(argc<3)
            help();
        {
            success=FDS_readDisk(NULL, argv[2]);
            break;
        }

    case 'R':   //readRaw -R file
        if(argc<3)
            help();
        {
            success=FDS_readDisk(argv[2], NULL);
            break;
        }
/*
    case 'D':   //dump -D filename addr size
        if(argc<3)
            help();
        {
            int addr=0, size=0x80000;
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

    case 'R':   //reset -R
        {
            success=dev_reset();
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
