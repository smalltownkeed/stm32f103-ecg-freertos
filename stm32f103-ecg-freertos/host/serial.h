#ifndef ECG_SERIAL_H
#define ECG_SERIAL_H
#include <windows.h>
#include <stdint.h>
HANDLE serial_open(const char *port);
int serial_read(HANDLE handle, uint8_t *bytes, unsigned capacity);
int serial_write(HANDLE handle, const uint8_t *bytes, unsigned size);
#endif
