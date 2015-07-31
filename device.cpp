#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include "hidapi/hidapi.h"
#include "device.h"
#include "spi.h"

#define VID 0x16d0
#define PID 0x0aaa
#define DEV_NAME L"FDSStick"

int dev_flashSize;
int dev_slots;
uint8_t dev_fwVersion;

static hid_device *handle=NULL;
static uint8_t hidbuf[256];


bool dev_open() {
    struct hid_device_info *devs, *cur_dev;

    dev_close();
    devs = hid_enumerate(VID, PID);
    cur_dev = devs;
    while (cur_dev) {
        if(cur_dev->vendor_id==VID && cur_dev->product_id==PID && cur_dev->product_string && wcscmp(DEV_NAME, cur_dev->product_string)==0)
            break;
        cur_dev = cur_dev->next;
    }
    if(cur_dev)
        handle = hid_open_path(cur_dev->path);
    if(handle) {
        dev_fwVersion = cur_dev->release_number&0xff;
        dev_flashSize = spi_readFlashSize();
        dev_slots = dev_flashSize/SLOTSIZE;
        wprintf(L"Opened %s (%04X:%04X:%04X:%s:%dM)\n", cur_dev->product_string, cur_dev->vendor_id, cur_dev->product_id, cur_dev->release_number, cur_dev->serial_number, dev_flashSize/0x20000);
        if(!dev_flashSize) {
            printf("Flash read failed.\n");
            dev_close();
        }
    } else {
        printf("Device not found.\n");
    }
    hid_free_enumeration(devs);
    return !!handle;
}

void dev_close() {
    dev_flashSize=0;
    dev_slots=0;
    if(handle) {
        hid_close(handle);
    }
    handle=NULL;
}

void dev_printLastError() {
    const wchar_t *err=hid_error(handle);
    if(err)
        printf("hidapi: %ls\n", err);
}

bool dev_reset() {
    hidbuf[0]=ID_RESET;
    hid_send_feature_report(handle, hidbuf, 2);    //reset will cause an error, ignore it
    return true;
}

bool dev_writeStart() {
    hidbuf[0]=ID_DISK_WRITE_START;
    return hid_send_feature_report(handle, hidbuf, 2) >= 0;
}

bool dev_updateFirmware() {
    hidbuf[0]=ID_UPDATEFIRMWARE;
    hid_send_feature_report(handle, hidbuf, 2);    //reset after update will cause an error, ignore it
    return true;
}

void dev_selfTest() {
    hidbuf[0]=ID_SELFTEST;
    hid_send_feature_report(handle, hidbuf, 2);
}

bool dev_spiRead(uint8_t *buf, int size, bool holdCS) {
    if(size>SPI_READMAX)
        { printf("Read too big.\n"); return false; }
    hidbuf[0]=holdCS? ID_SPI_READ: ID_SPI_READ_STOP;
    if(hid_get_feature_report(handle, hidbuf, 64) < 0)
        return false;
    memcpy(buf, hidbuf+1, size);
    return true;
}

bool dev_spiWrite(uint8_t *buf, int size, bool initCS, bool holdCS) {
    if(size>SPI_WRITEMAX)
        { printf("Write too big.\n"); return false; }
    hidbuf[0]=ID_SPI_WRITE;
    hidbuf[1]=size;
    hidbuf[2]=initCS,
    hidbuf[3]=holdCS;
    if(size)
        memcpy(hidbuf+4, buf, size);
    return hid_send_feature_report(handle, hidbuf, 4+size) >= 0;
}

//---------

static uint8_t read_sequence;

bool dev_readStart() {
    hidbuf[0]=ID_DISK_READ_START;
    read_sequence=1;
    return hid_send_feature_report(handle, hidbuf, 2) >= 0;
}

//Returns read size: <0 on error, <DISK_READMAX at end of disk.
int dev_readDisk(uint8_t *buf) {
    hidbuf[0]=ID_DISK_READ;
    int result=hid_get_feature_report(handle, hidbuf, DISK_READMAX+2);  // + reportID + sequence
    if(result<2) {
        return -1;      //timed out / bad read
    } else if(result>2) {  //adapter will send incomplete/empty packets when it's out of data (end of disk)
        memcpy(buf, hidbuf+2, result-2);
        if(hidbuf[1]!=read_sequence++) {    //sequence out of order (data lost)
            return -1;
        } else {
            return result-2;
        }
    } else {
        return 0;
    }
}

bool dev_writeDisk(uint8_t *buf, int size) {
    if(size!=DISK_WRITEMAX)        //always max!
        return false;
    hidbuf[0]=ID_DISK_WRITE;
    memcpy(hidbuf+1, buf, size);
    return hid_write(handle, hidbuf, DISK_WRITEMAX+1) >= 0;     // WRITEMAX+reportID
}
