#ifndef _TEXTURE_CALLBACKS_H_
#define _TEXTURE_CALLBACKS_H_

// Read callbacks
uint32_t readR(void *data);
uint32_t readRG(void *data);
uint32_t readRGB(void *data);
uint32_t readRGBA(void *data);

// Write callbacks
void writeR(void *data, uint32_t color);
void writeRGB(void *data, uint32_t color);
void writeRGBA(void *data, uint32_t color);

#endif