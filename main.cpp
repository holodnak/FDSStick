#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include "device.h"
#include "spi.h"
#include "fds.h"
#include "firmware.h"
#include "os.h"

bool FW_writeFlash(char *filename)
{
	FILE *fp;
	uint8_t *buf = 0;
	uint32_t *buf32, chksum;
	int i, filesize, pos;

	if ((fp = fopen(filename, "rb")) == 0) {
		printf("unable to open firmware '%s'\n", filename);
		return(false);
	}
	fseek(fp, 0, SEEK_END);
	filesize = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	if (filesize > (0x8000 - 8)) {
		printf("firmware image too large\n");
		fclose(fp);
		return(false);
	}
	buf = (uint8_t*)malloc(0x8000);
	buf32 = (uint32_t*)buf;
	memset(buf, 0, 0x8000);
	fread(buf, 1, filesize, fp);
	fclose(fp);

	buf32[(0x8000 - 8) / 4] = 0xDEADBEEF;

	chksum = 0;
	for (i = 0; i<(0x8000 - 4); i += 4) {
		chksum ^= buf32[i / 4];
	}

	printf("firmware is %d bytes, checksum is $%08X\n", filesize, chksum);

	buf32[(0x8000 - 4) / 4] = chksum;

	printf("uploading new firmware");
	if (!spi_writeFlash2(buf, 0x8000, 0x8000)) {
		printf("Write failed.\n");
		return false;
	}
	free(buf);

	printf("waiting for device to reboot\n");

	dev_updateFirmware();   //start update, device will reset itself
	sleep_ms(5000);

	if (!dev_open()) {
		printf("Open failed.\n");
		return false;
	}
	printf("Updated to build %d\n", dev_fwVersion);

	return true;
}

void app_exit(int exitcode) {
    dev_close();
//	 system("pause");
    exit(exitcode);
};

void help() {
    printf(
       "\n"
       "    -f file.fds [1..n]          write to flash (disk slot# 1..n)\n"
       "    -s file.fds [1..n]          read from flash\n"

		 "    -r file.fds                 read disk\n"
       "    -R file.raw [file.bin]      read disk (raw)\n"
       "    -w file.fds                 write disk\n"

		 "    -l                          list flash contents\n"

		 "    -L file.fds                 update the loader in slot 0\n"
		 "    -U file.bin                 update the firmware\n"

		 "    -e [1..8 | all]             erase flash\n"
       "    -D file [addr] [size]       dump flash\n"
       "    -W file [addr]              write flash\n"

		 "    -c file.fds file.bin        convert fds format to bin format\n"
		 "    -C file.fds file.raw        convert fds format to raw03 format\n"
		 "    -F file.bin file.fds        convert bin format to fds format\n"
		 );
    app_exit(1);
}

bool FDS_bintofds(char *filename, char *out);

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

	 case 'F': //convert file.bin file.fds
		 FDS_bintofds(argv[2], argv[3]);
		 break;

	 case 'c': //convert file.fds file.bin
		 FDS_convertDisk(argv[2], argv[3]);
		 break;

	 case 'C': //convert file.fds file.bin
		 FDS_convertDiskraw03(argv[2], argv[3]);
		 break;


	 case 'f': //flash -f file.fds [slot]
		 if (argc<3)
			 help();
		 {
			 int slot = 1;
			 if (argc>3)
				 sscanf(argv[3], "%i", &slot);
			 success = FDS_writeFlash(argv[2], slot);
		 }
		 app_exit(0);
		 break;

	 case 'U': //flash -f file.fds [slot]
		 if (argc<3)
			 help();
		 {
			 success = FW_writeFlash(argv[2]);
		 }
		 app_exit(0);
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
					 printf("erasing slot %d\n",slot);
					 if (slot > 0) {
						 success = spi_erasePage(SLOTSIZE*(slot-1));
					 }
					 else if (slot == 0) {
						 printf("cannot erase the loader\n");
					 }
                //TODO - erase all slots of a game
            }
        }
        break;

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

/*	case 'u': //update -u filename
		  if(argc<3)
            help();
        {
            if(spi_writeFile(argv[2], 0xff0000))
                success=dev_updateFirmware();
            break;
        }
		  */
    case 'T':   //mfgTest -T ...
        {
            dev_selfTest();
            success=true;
            break;
        }


    default:
        help();
    }

    printf(success? "Ok.\n": "Failed.\n");
    if(!success)
        dev_printLastError();

    app_exit(success?0:1);
}
