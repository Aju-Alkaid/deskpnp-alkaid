#ifndef _DRIVER_SPIFLASH_W25Q64_H
#define _DRIVER_SPIFLASH_W25Q64_H

#include <stdint.h>
#include "cmsis_os2.h"

extern osMutexId_t w25q64_mutex;

void W25Q64_Init(void);
int W25Q64_Read(uint32_t offset, uint8_t *buf, uint32_t len);
int W25Q64_Write(uint32_t offset, uint8_t *buf, uint32_t len);
int W25Q64_Erase(uint32_t offset, uint32_t len);
void W25Q64_Test(void);


#endif /* _DRIVER_SPIFLASH_W25Q64_H */