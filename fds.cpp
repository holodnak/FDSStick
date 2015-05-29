#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "device.h"
#include "fds.h"
#include "spi.h"
#include "os.h"

/*
Disk format in flash:
struct {
    uint16_t filename[120];  //null terminated unicode string.  filename[0]: 0xFFFF=empty, 0x0000=multi-disk image (continued from previous)
    uint8_t reserved[14];   //set to 0
    uint16_t lead_in;       //lead-in length (#bits), 0=default
    uint8_t data[0xff00];   //disk data, beginning with gap end mark (0x80) of first block
}
*/

enum {
    DEFAULT_LEAD_IN=28300,      //#bits
    GAP=976/8-1,
    MIN_GAP_SIZE=0x300,         //bits
    FDSSIZE=65500,          //size of .fds disk side, excluding header
    RAWSIZE=0x10000-256,    //size of image in flash, excluding header
    FLASHHEADERSIZE=0x100,
};

//don't include gap end
uint16_t calc_crc(uint8_t *buf, int size) {
    uint32_t crc=0x8000;
    int i;
    while(size--) {
        crc |= (*buf++)<<16;
        for(i=0;i<8;i++) {
            if(crc & 1) crc ^= 0x10810;
            crc>>=1;
        }
    }
    return crc;
}

void copy_block(uint8_t *dst, uint8_t *src, int size) {
    dst[0] = 0x80;
    memcpy(dst+1, src, size);
    uint32_t crc = calc_crc(dst+1, size+2);
    dst[size+1]=crc;
    dst[size+2]=crc>>8;
}

//Adds GAP + GAP end (0x80) + CRCs to .FDS image
//Returns size (0=error)
int fds_to_raw(uint8_t *dst, uint8_t *src) {
    int i=0, o=0;

    //check *NINTENDO-HVC* header
    if(src[0]!=0x01 || src[1]!=0x2a || src[2]!=0x4e) {
        printf("Not an FDS file.\n");
        return 0;
    }
    memset(dst, 0, RAWSIZE);

    //block type 1
    copy_block(dst+o, src+i, 0x38);
    i+=0x38;
    o+=0x38+3+GAP;

    //block type 2
    copy_block(dst+o, src+i, 2);
    i+=2;
    o+=2+3+GAP;

    //block type 3+4...
    while(src[i]==3) {
        uint32_t size = (src[i+13] | (src[i+14]<<8))+1;
        if(o + 16+3 + GAP + size+3 > RAWSIZE) {
            printf("Out of space (%d bytes short), adjust GAP size?\n",(o + 16+3 + GAP + size+3)-RAWSIZE);
            return 0;
        }
        copy_block(dst+o, src+i, 16);
        i+=16;
        o+=16+3+GAP;

        copy_block(dst+o, src+i, size);
        i+=size;
        o+=size+3+GAP;
    }
    return o;
/*
    int bytesRemaining=FDSSIZE-i;
    int spaceLeft=RAWSIZE-o;
    if(spaceLeft>0) {
        memcpy(dst+o, src+i, bytesRemaining>spaceLeft? spaceLeft: bytesRemaining);
        for(int tmp=i; tmp<FDSSIZE;tmp++) {
            if(src[tmp]!=0) {
                printf("Warning! Extra unrecognized data, appending as raw.\n");
                break;
            }
        }
    }
*/
}


//look for pattern of bits matching the first disk block
static int findFirstBlock(uint8_t *buf) {
    static const uint8_t dat[]={1,0,1,0,0,0,0,0, 0,1,2,2,1,0,1,0, 0,1,1,2,1,1,1,1, 1,1,0,0,1,1,1,0};
    int i,len;
    for(i=0, len=0; i<0x2000*8; i++) {
        if(buf[i]==dat[len]) {
            len++;
            if(len==sizeof(dat))
                return i-len;
        } else {
            i-=len;
            len=0;
        }
    }
    return 0;
}

bool block_decode(uint8_t *dst, uint8_t *src, int *inP, int *outP, int srcSize, int dstSize, int blockSize, char blockType) {
    if(*outP+blockSize+2 > dstSize) {
        printf("Out of space\n");
        return false;
    }

    int in=*inP;
    int outEnd=(*outP+blockSize+2)*8;
    int out=(*outP)*8;
    int start;

    //scan for gap end
    for(int zeros=0; src[in]!=1 || zeros<MIN_GAP_SIZE; in++) {
        if(src[in]==0) {
            zeros++;
        } else {
            zeros=0;
        }
        if(in>=srcSize-2)
            return false;
    }
    start=in;

    char bitval=1;
    in++;
    do {
        if(in>=srcSize) {   //not necessarily an error, probably garbage at end of disk
            //printf("Disk end\n"); 
            return false;
        }
        switch(src[in]|(bitval<<4)) {
            case 0x11:
                out++;
            case 0x00:
                out++;
                bitval=0;
                break;
            case 0x12:
                out++;
            case 0x01:
            case 0x10:
                dst[out/8] |= 1<<(out&7);
                out++;
                bitval=1;
                break;
            default: //Unexpected value.  Keep going, we'll probably get a CRC warning
                //printf("glitch(%d) @ %X(%X.%d)\n", src[in], in, out/8, out%8);
                out++;
                bitval=0;
                break;
        }
        in++;
    } while(out<outEnd);
    if(dst[*outP]!=blockType) {
        printf("Wrong block type %X(%X)-%X(%X)\n", start, *outP, in, out-1);
        return false;
    }
    out=out/8-2;

    printf("Out%d %X(%X)-%X(%X)\n", blockType, start, *outP, in, out-1);

    if(calc_crc(dst+*outP,blockSize+2)) {
        uint16_t crc1=(dst[out+1]<<8)|dst[out];
        dst[out]=0;
        dst[out+1]=0;
        uint16_t crc2=calc_crc(dst+*outP,blockSize+2);
        printf("Bad CRC (%04X!=%04X)\n", crc1, crc2);
    }

    dst[out]=0;     //clear CRC
    dst[out+1]=0;
    dst[out+2]=0;   //+spare bit
    *inP=in;
    *outP=out;
    return true;
}

static bool raw_to_fds(uint8_t *raw, uint8_t *fds, int rawsize) {
    int i;
    int in,out;

    //change timestamps to 0..3
    for(i=0; i<rawsize; ++i) {
        if(raw[i]<0x30) raw[i]=3;
        else if(raw[i]<0x50) raw[i]=0;
        else if(raw[i]<0x70) raw[i]=1;
        else if(raw[i]<0xa0) raw[i]=2;
        else raw[i]=3;
    }

    memset(fds,0,FDSSIZE);
/*
    FILE *f=fopen("raw0-3.bin","wb");
    fwrite(raw,1,rawsize,f);
    fclose(f);
*/
    //lead-in can vary a lot depending on drive, scan for first block to get our bearings
    in=findFirstBlock(raw)-MIN_GAP_SIZE;
    if(in<0)
        return false;

    out=0;
    if(!block_decode(fds, raw, &in, &out, rawsize, FDSSIZE+2, 0x38, 1))
        return false;
    if(!block_decode(fds, raw, &in, &out, rawsize, FDSSIZE+2, 2, 2))
        return false;
    do {
        if(!block_decode(fds, raw, &in, &out, rawsize, FDSSIZE+2, 16, 3))
            return true;
        if(!block_decode(fds, raw, &in, &out, rawsize, FDSSIZE+2, 1+(fds[out-16+13] | (fds[out-16+14]<<8)), 4))
            return true;
    } while(in<rawsize);
    return true;
}

//Only handles one side...
bool FDS_readDisk(char *filename_raw, char *filename_fds) {
    enum { READBUFSIZE=0x90000 };

    FILE *f;
    uint8_t *readBuf=NULL;
    uint8_t *fds=NULL;
    int result;
    int bytesIn=0;
    uint8_t sequence=1;

    //if(!(dev_readIO()&MEDIA_SET)) {
    //    printf("Warning - Disk not inserted?\n");
    //}
    if(!dev_readStart())
        return false;

    readBuf=(uint8_t*)malloc(READBUFSIZE);
    do {
        result=dev_readDisk(readBuf+bytesIn);
        bytesIn+=result;
        if(!(bytesIn%((DISK_READMAX)*32)))
            printf(".");
    } while(result==DISK_READMAX && bytesIn<READBUFSIZE-DISK_READMAX);
    printf("\n");
    if(result<0) {
        printf("Read error.\n");
        return false;
    }

    if(filename_raw) {
        if( (f=fopen(filename_raw,"wb")) ) {
            fwrite(readBuf, 1, bytesIn, f);
            fclose(f);
            printf("Wrote %s\n",filename_raw);
        }
    }

    fds=(uint8_t*)malloc(FDSSIZE+16);   //extra room for CRC junk
    raw_to_fds(readBuf, fds, bytesIn);
    if(filename_fds) {
        if( (f=fopen(filename_fds,"wb")) ) {
            fwrite(fds, 1, FDSSIZE, f);
            fclose(f);
            printf("Wrote %s\n",filename_fds);
        }
    }
    free(fds);
    free(readBuf);
    return true;
}

static bool writeDisk(uint8_t *buf, int size) {
    int bytesOut;

    //if(!(readIO()&MEDIA_SET)) {
    //    printf("Disk not inserted.\n");
    //}
    if(!dev_writeStart())
        return false;

    for(bytesOut=0; bytesOut<size; bytesOut+=DISK_WRITEMAX) {
        if(!dev_writeDisk(buf+bytesOut, DISK_WRITEMAX)) {
            printf("Write error (disk full?)\n");
            return false;
        }
        if(!(bytesOut%(DISK_WRITEMAX*16)))
            printf("#");
    }

    //Fill remainder with empty space.   Keep writing until we can't, EP0 will stall at end of disk
    memset(buf, 0xaa, DISK_WRITEMAX);
    for(bytesOut=0; bytesOut<0x20000; bytesOut+=DISK_WRITEMAX) {
        if(!dev_writeDisk(buf, DISK_WRITEMAX))
            break;
        if(!(bytesOut%(DISK_WRITEMAX*16)))
            printf(".");
    }
    return true;
}

bool FDS_writeDisk(char *name) {
    static const uint8_t expand[]={ 0xaa, 0xa9, 0xa6, 0xa5, 0x9a, 0x99, 0x96, 0x95, 0x6a, 0x69, 0x66, 0x65, 0x5a, 0x59, 0x56, 0x55 };
    enum { WRITESIZE=(RAWSIZE+DEFAULT_LEAD_IN/8)*2, };
    uint8_t *inbuf=0;       //.FDS buffer
    uint8_t *raw=0;         //.FDS with gaps/CRC
    uint8_t *outbuf=0;      //bitstream
    FILE *f;

    f=fopen(name,"rb");
    if(!f) { printf("Can't open %s\n", name); return false; }

    fseek(f,0,SEEK_END);
    int filesize=ftell(f);
    fseek(f,0,SEEK_SET);
    filesize=16 + FDSSIZE*((filesize+FDSSIZE-17)/FDSSIZE);  //pad up to whole disk size

    inbuf=(uint8_t*)malloc(filesize);
    outbuf=(uint8_t*)malloc(WRITESIZE);
    raw=(uint8_t*)malloc(RAWSIZE);

    memset(inbuf,0,filesize);
    fread(inbuf,1,filesize,f);
    fclose(f);

    int inpos=0, side=0;
    if(inbuf[0]=='F')
        inpos=16;      //skip fwNES header

    char prompt;
    do {
        printf("Side %d\n", side+1);

        int imgsize=fds_to_raw(raw, inbuf+inpos);
        if(!imgsize)
            break;

        //expand raw to bitpattern
        memset(outbuf, 0xaa, WRITESIZE);
        for(int i=0;i<imgsize;i++) {
            outbuf[DEFAULT_LEAD_IN/8*2 + i*2 + 0]=expand[raw[i]&0x0f];
            outbuf[DEFAULT_LEAD_IN/8*2 + i*2 + 1]=expand[(raw[i]>>4)&0x0f];
        }

        if(!writeDisk(outbuf, (imgsize+DEFAULT_LEAD_IN/8)*2))
            break;
        inpos+=FDSSIZE;
        side++;

        //prompt for disk change
        prompt=0;
        if(inpos<filesize && inbuf[inpos]==0x01) {
            printf("Push ENTER for next disk side\n");
            prompt=readKb();
        }
    } while(prompt==0x0d);

    free(raw);
    free(outbuf);
    free(inbuf);
    return true;
}

bool FDS_writeFlash(char *name, int slot) {
    enum { FILENAMELENGTH=120, };   //number of characters including null

    uint8_t *inbuf=0;
    uint8_t *outbuf=0;
    FILE *f;

    f=fopen(name,"rb");
    if(!f) return false;

    fseek(f,0,SEEK_END);
    int filesize=ftell(f);
    fseek(f,0,SEEK_SET);
    filesize=16 + FDSSIZE*((filesize+FDSSIZE-17)/FDSSIZE);  //pad up to whole disk size

    inbuf=(uint8_t*)malloc(filesize);
    outbuf=(uint8_t*)malloc(0x10000);
    memset(inbuf,0,filesize);
    fread(inbuf,1,filesize,f);
    fclose(f);

    int pos=0, side=0;
    if(inbuf[0]=='F')
        pos=16;      //skip fwNES header

    do {
        printf("Side %d\n", side+1);
        if(fds_to_raw(outbuf+FLASHHEADERSIZE, inbuf+pos)) {
            memset(outbuf,0,FLASHHEADERSIZE);
            outbuf[0xfe]=DEFAULT_LEAD_IN & 0xff;
            outbuf[0xff]=DEFAULT_LEAD_IN / 256;
            if(side==0) {
                //strip path from filename
                char *shortName=strrchr(name,'/');      // ...dir/file.fds
#ifdef _WIN32
                if(!shortName)
                    shortName=strrchr(name,'\\');        // ...dir\file.fds
                if(!shortName)
                    shortName=strchr(name,':');         // C:file.fds
#endif
                if(!shortName)
                    shortName=name;
                else
                    shortName++;
                utf8_to_utf16((uint16_t*)outbuf, shortName, FILENAMELENGTH*2);
                ((uint16_t*)outbuf)[FILENAMELENGTH-1]=0;
            }
            spi_writeFlash(outbuf, (slot+side)*0x10000, 0x10000);
        }
        pos+=FDSSIZE;
        side++;
    } while(pos<filesize && inbuf[pos]==0x01);
    free(inbuf);
    free(outbuf);
    return true;
}

bool FDS_list() {
    uint8_t buf[256];
    int side=0;
    for(int slot=1;slot<=8;slot++) {
        if(!spi_readFlash((slot-1)*0x10000,buf,256))
            return false;
        if(buf[0]==0xff) {
            printf("%d:\n",slot);
        } else if(buf[0]!=0) {
            wprintf(L"%d: %s\n", slot, buf);
            side=1;
        } else {
            printf("%d:    Side %d\n", slot, ++side);
        }
    }
    return true;
}
