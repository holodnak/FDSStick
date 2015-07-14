#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include "device.h"
#include "os.h"


enum {
    PAGESIZE=256,
    CMD_READSTATUS=0x05,
    CMD_WRITEENABLE=0x06,
    CMD_READID=0x9f,
    CMD_READDATA=0x03,
    CMD_WRITESTATUS=0x01,
    CMD_PAGEWRITE=0x0a,
    CMD_PAGEERASE=0xdb,
};

uint32_t spi_readID() {
    static uint8_t readID[]={CMD_READID};
    uint32_t id=0;
    if(!dev_spiWrite(readID, 1, 1, 1))
        return 0;
    if(!dev_spiRead((uint8_t*)&id, 3, 0))
        return 0;
    return id;
}

uint32_t spi_readFlashSize() {
    uint32_t id=spi_readID();
    switch(id) {
        case 0x138020: // ST25PE40, M25PE40: 4Mbit (512kB)
            return 0x80000;
    }
    return 0;
}

bool spi_readFlash(int addr, uint8_t *buf, int size) {
    static uint8_t cmd[4]={CMD_READDATA,0,0,0};
    cmd[1]=addr>>16;
    cmd[2]=addr>>8;
    cmd[3]=addr;
    if(!dev_spiWrite(cmd,4,1,1))
        return false;
    for(;size>0;size-=SPI_READMAX) {
        if(!dev_spiRead(buf, size>SPI_READMAX? SPI_READMAX: size, size>SPI_READMAX))
            return false;
        buf+=SPI_READMAX;
    }
    return true;
}

bool spi_dumpFlash(char *filename, int addr, int size) {
    uint8_t *buf=NULL;
    FILE *f=NULL;
    bool ok=false;

    do {
        f=fopen(filename, "wb");
        if(!f)
            { printf("Can't open %s\n",filename); break; }
        buf=(uint8_t*)malloc(size);
        if(!spi_readFlash(addr, buf, size))
            break;
        fwrite(buf, 1, size, f);
        printf("Dumped %s (0x%X-0x%X)\n",filename, addr, addr+size-1);
        ok=true;
    } while(0);

    if(f)
        fclose(f);
    if(buf)
        free(buf);
    if(!ok)
        printf("Read failed!\n");
    return ok;
}

static bool readStatus(uint8_t *status) {
    static uint8_t cmd[]={CMD_READSTATUS};
    if(!dev_spiWrite(cmd,1,1,1))
        return false;
    return dev_spiRead(status,1,0);
}

static bool writeEnable() {
    static uint8_t cmd[]={CMD_WRITEENABLE};
    return dev_spiWrite(cmd,1,1,0);
}

//wait for write-in-progress to end
//fail on timeout or read failure
static bool writeWait(uint32_t timeout_ms) {
    static uint8_t cmd[]={CMD_READSTATUS};
    uint8_t status;

    if(!dev_spiWrite(cmd,1,1,1))
        return false;

    uint32_t start=getTicks();
    do {
        if(!dev_spiRead(&status,1,1))
            return false;
    } while((status&1) && (getTicks()-start < timeout_ms));
    if(!dev_spiWrite(0,0,0,0)) // CS release
        return false;
    return !(status&1);
}

static bool unWriteProtect() {
    static uint8_t cmd[]={CMD_WRITESTATUS,0};

    uint8_t status;
    if(!readStatus(&status))
        return false;
    if(!(status & 0x1c))    //already unlocked
        return true;
    if(!writeEnable())
        return false;
    if(!dev_spiWrite(cmd,2,1,0))
        return false;
    return writeWait(50);
}

//write single page
static bool pageWrite(uint32_t addr, uint8_t *buf, int size) {
    uint8_t cmd[PAGESIZE+4];
    if(((addr&(PAGESIZE-1))+size)>PAGESIZE)
        { printf("Page write overflow.\n"); return false; }
    if(!writeEnable())
        return false;
    cmd[0]=CMD_PAGEWRITE;
    cmd[1]=addr>>16;
    cmd[2]=addr>>8;
    cmd[3]=addr;
    memcpy(cmd+4, buf, size);
    size+=4;

    uint8_t *p=cmd;
    for(;size>0;size-=SPI_WRITEMAX) {
        if(!dev_spiWrite(p, size>SPI_WRITEMAX? SPI_WRITEMAX: size, p==cmd, size>SPI_WRITEMAX))
            return false;
        p+=SPI_WRITEMAX;
    }
    return writeWait(50);
}

bool spi_writeFlash(uint8_t *buf, uint32_t addr, uint32_t size) {
    uint32_t wrote, pageWriteSize;
    bool ok=false;
    do {
        if(!unWriteProtect())
            { printf("Write protected.\n"); break; }
        for(wrote=0; wrote<size; wrote+=pageWriteSize) {
            pageWriteSize=PAGESIZE-(addr & (PAGESIZE-1));   //bytes left in page
            if(pageWriteSize>size-wrote)
                pageWriteSize=size-wrote;
            if(!pageWrite(addr+wrote, buf+wrote, pageWriteSize))
                break;
            if((addr+wrote)%0x800==0)
                printf(".");
        }
        printf("\n");
        ok=(wrote==size);
    } while(0);
    return ok;
}

bool spi_writeFile(char *filename, uint32_t addr) {
    uint8_t *filebuf;
    uint32_t filesize;

    FILE *f=fopen(filename, "rb");
    if(!f) {
        printf("Can't open %s\n",filename);
        return false;
    }

    filebuf = (uint8_t*)malloc(dev_flashSize);
    filesize=fread(filebuf, 1, dev_flashSize, f);
    fclose(f);

    bool result=spi_writeFlash(filebuf, addr, filesize);
    free(filebuf);
    return result;
}

bool spi_erasePage(int addr) {
    uint8_t cmd[4];
    if(!unWriteProtect())
        { printf("Write protected.\n"); return false; }
    if(!writeEnable())
        return false;
    cmd[0]=CMD_PAGEERASE;
    cmd[1]=addr>>16;
    cmd[2]=addr>>8;
    cmd[3]=addr;
    if(!dev_spiWrite(cmd, 4, 1, 0))
        return false;
    return writeWait(50);
}
