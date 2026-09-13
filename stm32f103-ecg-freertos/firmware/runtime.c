#include <stddef.h>
void *memset(void *destination, int value, size_t size)
{
    unsigned char *p = destination;
    while (size--) *p++ = (unsigned char)value;
    return destination;
}
void *memcpy(void *destination, const void *source, size_t size)
{
    unsigned char *to = destination;
    const unsigned char *from = source;
    while (size--) *to++ = *from++;
    return destination;
}
void *memmove(void *destination, const void *source, size_t size)
{
    unsigned char *to = destination;
    const unsigned char *from = source;
    if (to < from) while (size--) *to++ = *from++;
    else while (size) { size--; to[size] = from[size]; }
    return destination;
}
