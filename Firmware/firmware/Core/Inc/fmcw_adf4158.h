#ifndef FMCW_ADF4158_H
#define FMCW_ADF4158_H

#include <stdint.h>

void fmcw_adf4158_init(void);
void fmcw_adf4158_program_default(void);
void fmcw_adf4158_write32(uint32_t word);

#endif

