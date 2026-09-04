#ifndef INFLATE_H
#define INFLATE_H
#include <stdint.h>
#include <stddef.h>
uint8_t *gunzip(const uint8_t *data, size_t len, size_t *outLen);
#endif
