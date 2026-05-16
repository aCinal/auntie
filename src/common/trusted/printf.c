#include <stdio.h>
#include "printf.h"
#include "ocall.h"

void printf(const char *fmt, ...)
{
    char buffer[8 * 1024];
    va_list ap;
    va_start(ap, fmt);
    (void) vsnprintf(buffer, sizeof(buffer), fmt, ap);
    va_end(ap);
    /* NOTE: We do not use the OCALL macro to not get infinite recursion on OCALL failure.
     * Also, ignore any errors, if we can't print, too bad. */
    ocall_print(buffer);
}
