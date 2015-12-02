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
	CMD_READDATA = 0x03,
	CMD_WRITEDATA = 0x02,
	CMD_WRITESTATUS=0x01,
	CMD_PAGEWRITE = 0x0a,
	CMD_PAGEERASE = 0xdb,
	CMD_PAGEPROGRAM = 0x02,
	CMD_BLOCKERASE = 0xd8,
	CMD_BLOCKERASE64 = CMD_BLOCKERASE,
	CMD_BLOCKERASE32 = 0x52,
	CMD_SECTORERASE = 0x20,
};

uint32_t spi_readID() {
    static uint8_t readID[]={CMD_READID};
    uint32_t id=0;
	 if (!dev_spiWrite(readID, 1, 1, 1)) {
		 printf("spi_readID: dev_spiWrite failed\n");
		 return 0;
	 }
	 if (!dev_spiRead((uint8_t*)&id, 3, 0)) {
		 printf("spi_readID: dev_spiRead failed\n");
		 return 0;
	 }
    return id;
}

uint32_t spi_readFlashSize() {
	uint32_t id=spi_readID();
	printf("Flash ID is $%X\n", id);
	switch(id) {
		case 0x138020: // ST25PE40, M25PE40: 4Mbit (512kB)
			return 0x80000;
		case 0x1440EF: // W25Q80DV (1mB)
			return 0x100000;
		case 0x174001: // S25FL164K (8mB)
			return 0x800000;
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
	 bool ret;

    if(!dev_spiWrite(cmd,1,1,1))
        return false;
    ret = dev_spiRead(status,1,0);
	 printf("readStatus:  status = %X\n", *status);
	 return ret;
}

static bool writeEnable() {
    static uint8_t cmd[]={CMD_WRITEENABLE};
//	 printf("enabling writes.\n");
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

/*    uint8_t status;
    if(!readStatus(&status))
        return false;
	 if (!(status & 0x1c)) {    //already unlocked
		 printf("unWriteProtect: already unlocked\n");
		 return true;
	 }*/
	 if (!writeEnable()) {
		 printf("write enable failed.\n");
		 return false;
	 }
//	 printf("write enable ok.\n");
	 if(!dev_spiWrite(cmd,2,1,0))
        return false;
    return writeWait(50);
}

//write single page
static bool pageWrite(uint32_t addr, const uint8_t *buf, int size) {
	uint8_t cmd[PAGESIZE + 4];
	if (((addr&(PAGESIZE - 1)) + size)>PAGESIZE)
	{
		printf("Page write overflow.\n"); return false;
	}
	if (!writeEnable())
		return false;
	cmd[0] = CMD_PAGEWRITE;
	cmd[1] = addr >> 16;
	cmd[2] = addr >> 8;
	cmd[3] = addr;
	memcpy(cmd + 4, buf, size);
	size += 4;

	uint8_t *p = cmd;
	for (; size>0; size -= SPI_WRITEMAX) {
		if (!dev_spiWrite(p, size>SPI_WRITEMAX ? SPI_WRITEMAX : size, p == cmd, size>SPI_WRITEMAX))
			return false;
		p += SPI_WRITEMAX;
	}
	return writeWait(50);
}

static bool blockErase(uint32_t addr)
{
	uint8_t cmd[] = { CMD_BLOCKERASE,0,0,0 };
	if (!writeEnable())
		return false;
	cmd[1] = addr >> 16;
	if (!dev_spiWrite(cmd, 4, 1, 0))
		return false;
	return writeWait(2000);
}

static bool blockErase32(uint32_t addr)
{
	uint8_t cmd[] = { CMD_BLOCKERASE32,0,0,0 };
	if (!writeEnable())
		return false;
	cmd[1] = addr >> 16;
	cmd[2] = addr >> 8;
	if (!dev_spiWrite(cmd, 4, 1, 0))
		return false;
	return writeWait(1600);
}

static bool sectorErase(uint32_t addr)
{
	uint8_t cmd[] = { CMD_SECTORERASE,0,0,0 };
	if (!writeEnable())
		return false;
	cmd[1] = addr >> 16;
	cmd[2] = addr >> 8;
	if (!dev_spiWrite(cmd, 4, 1, 0))
		return false;
	return writeWait(600);
}

static bool pageProgram(uint32_t addr, const uint8_t *buf, int size) {
	uint8_t cmd[PAGESIZE + 4];
	if (((addr&(PAGESIZE - 1)) + size)>PAGESIZE)
	{
		printf("Page write overflow.\n"); return false;
	}
	if (!writeEnable())
		return false;
	cmd[0] = CMD_PAGEPROGRAM;
	cmd[1] = addr >> 16;
	cmd[2] = addr >> 8;
	cmd[3] = addr;
	memcpy(cmd + 4, buf, size);
	size += 4;

	uint8_t *p = cmd;
	for (; size>0; size -= SPI_WRITEMAX) {
		if (!dev_spiWrite(p, size>SPI_WRITEMAX ? SPI_WRITEMAX : size, p == cmd, size>SPI_WRITEMAX))
			return false;
		p += SPI_WRITEMAX;
	}
	return writeWait(50);
}

bool spi_writeFlash(const uint8_t *buf, uint32_t addr, uint32_t size) {
	uint32_t wrote, pageWriteSize;
	bool ok = false;
	do {
		if (blockErase(addr) == 0) {
			printf("spi_WriteFlash: blockErase failed\n");
			break;
		}
		if (!unWriteProtect())
		{
			printf("Write protected.\n"); break;
		}
		for (wrote = 0; wrote<size; wrote += pageWriteSize) {
			pageWriteSize = PAGESIZE - (addr & (PAGESIZE - 1));   //bytes left in page
			if (pageWriteSize>size - wrote)
				pageWriteSize = size - wrote;
			if (pageProgram(addr + wrote, buf + wrote, pageWriteSize) == 0) {
				printf("spi_WriteFlash: pageErase failed\n");
				break;
			}
			//				if (!pageWrite(addr + wrote, buf + wrote, pageWriteSize))
			//					break;
			if ((addr + wrote) % 0x800 == 0)
				printf(".");
		}
		printf("\n");
		ok = (wrote == size);
	} while (0);
	return ok;
}

//QUICK HACK :(
bool spi_writeFlash2(const uint8_t *buf, uint32_t addr, uint32_t size) {
	uint32_t wrote, pageWriteSize;
	bool ok = false;
	do {
		if (blockErase32(addr) == 0) {
			printf("spi_WriteFlash: blockErase32 failed\n");
			break;
		}
		if (!unWriteProtect())
		{
			printf("Write protected.\n"); break;
		}
		for (wrote = 0; wrote<size; wrote += pageWriteSize) {
			pageWriteSize = PAGESIZE - (addr & (PAGESIZE - 1));   //bytes left in page
			if (pageWriteSize>size - wrote)
				pageWriteSize = size - wrote;
			if (pageProgram(addr + wrote, buf + wrote, pageWriteSize) == 0) {
				printf("spi_WriteFlash2: pageProgram failed\n");
				break;
			}
			//				if (!pageWrite(addr + wrote, buf + wrote, pageWriteSize))
			//					break;
			if ((addr + wrote) % 0x800 == 0)
				printf(".");
		}
		printf("\n");
		ok = (wrote == size);
	} while (0);
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
    cmd[0]=CMD_BLOCKERASE;
    cmd[1]=addr>>16;
    cmd[2]=0;
    cmd[3]=0;
    if(!dev_spiWrite(cmd, 4, 1, 0))
        return false;
    return writeWait(2000);
}

bool spi_writeSram(const uint8_t *buf, uint32_t addr, int size) {
	static uint8_t cmd[4] = { CMD_WRITEDATA,0,0,0 };
	cmd[2] = addr >> 8;
	cmd[3] = addr;

	printf("outputting write command\n");
	if (!dev_sramWrite(cmd, 3, 1, 1))
		return false;

	for (; size>0; size -= SPI_WRITEMAX) {
		if (!dev_sramWrite((uint8_t*)buf, size>SPI_WRITEMAX ? SPI_WRITEMAX : size, 0, size>SPI_WRITEMAX))
			return false;
		buf += SPI_WRITEMAX;
	}
	return true;
}
