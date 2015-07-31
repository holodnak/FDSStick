#include <stdio.h>
#include <stdint.h>
#include "device.h"
#include "spi.h"
#include "os.h"

#include "firmware.inc"
#define FW_VER (firmware[5])

//Update old firmware
bool firmware_update() {
    if(dev_fwVersion < FW_VER) {
        printf("Updating firmware (V%d -> V%d)\n", dev_fwVersion, FW_VER);

        //TODO - back up flash

        if(!spi_writeFlash(firmware, 0xff0000, sizeof(firmware))) {
            printf("Write failed.\n");
            return false;
        }

        dev_updateFirmware();   //start update, device will reset itself
        sleep_ms(1000);

        if(!dev_open()) {
            printf("Open failed.\n");
            return false;
        } else if(dev_fwVersion < FW_VER) {
            printf("Update failed.\n");
            return false;
        }
    }
    return true;
}
