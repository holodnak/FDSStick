#pragma once
#include "hidapi/hidapi.h"

enum {
    SPI_WRITEMAX=64-4,
    SPI_READMAX=63,

    DISK_READMAX=254,
    DISK_WRITEMAX=255,

    //HID reportIDs
    ID_RESET=0xf0,
    ID_UPDATEFIRMWARE=0xf1,
    ID_SELFTEST=0xf2,

    ID_SPI_READ=1,
    ID_SPI_READ_STOP,
    ID_SPI_WRITE,
	ID_SRAM_READ=5,
	ID_SRAM_READ_STOP,
	ID_SRAM_WRITE,

    ID_READ_IO=0x10,
    ID_DISK_READ_START,
    ID_DISK_READ,
    ID_DISK_WRITE_START,
    ID_DISK_WRITE,

	ID_FIRMWARE_READ = 0x40,
	ID_FIRMWARE_WRITE,
	ID_FIRMWARE_UPDATE,
};

//These get filled on dev_open()
extern uint16_t dev_fwVersion;
extern int dev_flashSize;           //in bytes
extern int dev_slots;

bool dev_open();
void dev_close();
void dev_printLastError();

bool dev_reset();
bool dev_updateFirmware();
void dev_selfTest();
bool dev_spiRead(uint8_t *buf, int size, bool holdCS);
bool dev_spiWrite(uint8_t *buf, int size, bool initCS, bool holdCS);
bool dev_sramWrite(uint8_t *buf, int size, bool initCS, bool holdCS);
bool dev_readStart();
int dev_readDisk(uint8_t *buf);
bool dev_writeStart();
bool dev_writeDisk(uint8_t *buf, int size);

bool dev_fwWrite(uint8_t *buf, int size, bool initCS, bool holdCS);
