#ifndef __APP_VISION_H
#define __APP_VISION_H

#include <stdint.h>

void Vision_Init(void);
void CamUart_RecvCallback(uint8_t *data, int len);

#endif