#pragma once

//void FDStest(char *name);
bool FDS_readDisk(char *filename_raw, char *filename_bin, char *filename_fds);
bool FDS_writeDisk(char *name);
bool FDS_writeFlash(char *name, int slot);
bool FDS_list();
bool FDS_rawToBin(char *filename_raw, char *filename_bin);
bool FDS_readFlashToFDS(char *filename_fds, int slot);
