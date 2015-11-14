#pragma once

//void FDStest(char *name);
bool FDS_readDisk(char *filename_raw, char *filename_bin, char *filename_fds);
bool FDS_writeDisk(char *name);
bool FDS_writeFlash(char *name, int slot);
bool FDS_list();
bool FDS_rawToBin(char *filename_raw, char *filename_bin);
bool FDS_readFlashToFDS(char *filename_fds, int slot);

int fds_to_bin(uint8_t *dst, uint8_t *src, int dstSize);
bool FDS_convertDisk(char *filename, char *out);
bool FDS_convertDiskraw03(char *filename, char *out);
