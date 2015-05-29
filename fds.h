#pragma once

//void FDStest(char *name);
bool FDS_readDisk(char *filename_raw, char *filename_fds);
bool FDS_writeDisk(char *name);
bool FDS_writeFlash(char *name, int slot);
bool FDS_list();
