/*
   Forked from emu8950 v1.1.4 (emuadpcm.h) for the Y8960 cartridge, 2026 by madscient.
   See Y8960Opl2exCore.h for what the fork changes and why.
*/
#ifndef _Y8960_OPL2EX_ADPCM_H_
#define _Y8960_OPL2EX_ADPCM_H_

#include <stdint.h>

/* One block of the sample memory: addresses [base, base + size) are
** data[0 .. size). Writes land only in writable blocks. */
typedef struct __Y8960OPL_ADPCM_REGION {
  uint32_t base;
  uint32_t size;
  uint8_t *data;
  uint8_t writable;
} Y8960OPL_ADPCM_REGION;

typedef struct __Y8960OPL_ADPCM {
  uint32_t clk;

  uint8_t reg[0x20];

  /* The sample memory lives outside the circuit, as a list of blocks the
  ** caller owns, because the cartridge's two circuits divide one 256KB SRAM
  ** between them in more than one way. Addresses no block covers read 0 and
  ** drop writes. There is no sample ROM. */
  const Y8960OPL_ADPCM_REGION *regions;
  uint32_t region_count;

  uint8_t status;

  uint32_t start_addr;
  uint32_t stop_addr;
  uint32_t play_addr;  /* Current play address * 2 */
  uint32_t delta_addr; /* 16bit address */
  uint32_t delta_n;
  uint32_t play_addr_mask;

  uint8_t play_start;

  int32_t output[2];
  uint32_t diff;

} Y8960OPL_ADPCM;

Y8960OPL_ADPCM *Y8960OPL_ADPCM_new(uint32_t clk);
void Y8960OPL_ADPCM_reset(Y8960OPL_ADPCM *);
void Y8960OPL_ADPCM_delete(Y8960OPL_ADPCM *);
void Y8960OPL_ADPCM_writeReg(Y8960OPL_ADPCM *, uint32_t reg, uint32_t val);
int16_t Y8960OPL_ADPCM_calc(Y8960OPL_ADPCM *);
uint8_t Y8960OPL_ADPCM_status(Y8960OPL_ADPCM *);
void Y8960OPL_ADPCM_resetStatus(Y8960OPL_ADPCM *);
void Y8960OPL_ADPCM_setMemoryMap(Y8960OPL_ADPCM *, const Y8960OPL_ADPCM_REGION *regions, uint32_t count);
#endif
