#ifndef PDET_H
#define PDET_H

#include <stdint.h>

void PDET_Init(void);
uint32_t PDET_ReadRaw(uint32_t *raw);
void PDET_PrintOnce(void);

#endif /* PDET_H */
