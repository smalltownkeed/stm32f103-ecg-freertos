#include "serial.h"
#include <stdio.h>

HANDLE serial_open(const char *port)
{
    char path[64];
    snprintf(path, sizeof(path), "\\\\.\\%s", port);
    HANDLE handle = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (handle == INVALID_HANDLE_VALUE) return handle;
    DCB config = {0};
    config.DCBlength = sizeof(config);
    if (!GetCommState(handle, &config)) goto fail;
    config.BaudRate = 115200;
    config.ByteSize = 8;
    config.Parity = NOPARITY;
    config.StopBits = ONESTOPBIT;
    config.fBinary = TRUE;
    config.fParity = FALSE;
    config.fOutxCtsFlow = FALSE;
    config.fOutxDsrFlow = FALSE;
    config.fDtrControl = DTR_CONTROL_DISABLE;
    config.fRtsControl = RTS_CONTROL_DISABLE;
    config.fDsrSensitivity = FALSE;
    config.fOutX = FALSE;
    config.fInX = FALSE;
    config.fErrorChar = FALSE;
    config.fNull = FALSE;
    config.fAbortOnError = FALSE;
    if (!SetCommState(handle, &config)) goto fail;
    COMMTIMEOUTS timeout = {MAXDWORD, 0, 0, 0, 100};
    if (!SetCommTimeouts(handle, &timeout)) goto fail;
    SetupComm(handle, 16384, 4096);
    PurgeComm(handle, PURGE_RXCLEAR | PURGE_TXCLEAR);
    return handle;
fail:
    CloseHandle(handle);
    return INVALID_HANDLE_VALUE;
}

int serial_read(HANDLE handle, uint8_t *bytes, unsigned capacity)
{
    DWORD count = 0;
    if (!ReadFile(handle, bytes, capacity, &count, NULL)) return -1;
    return (int)count;
}

int serial_write(HANDLE handle, const uint8_t *bytes, unsigned size)
{
    DWORD count = 0;
    return WriteFile(handle, bytes, size, &count, NULL) && count == size;
}
