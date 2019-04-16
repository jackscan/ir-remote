#ifndef _DEBUG_H_
#define _DEBUG_H_

#include <stdbool.h>

void debug_init(void);
bool debug_char_pending(void);
char debug_getchar(void);
void debug_finish(void);

#define LOG(MSG, ...) printf_P(FSTR(MSG), ##__VA_ARGS__)
#define LOG_P(MSG) fputs_P(MSG, stdout)
#define LOGC(C) putchar(C)

#endif
