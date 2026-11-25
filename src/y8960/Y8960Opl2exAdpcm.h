/*
   The ADPCM-B of the Y8960 cartridge's OPL2EX block, 2026 by madscient.
   Started as a fork of emu8950 v1.1.4 (emuadpcm.h); see Y8960Opl2exAdpcm.c
   for what it follows now.
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

#define Y8960OPL_ADPCM_LPF_TAPS 25

typedef struct __Y8960OPL_ADPCM {
  uint32_t clk;

  uint8_t reg[0x20];

  /* The sample memory lives outside the circuit, as a list of blocks the
  ** caller owns, because the cartridge's two circuits divide one 256KB SRAM
  ** between them in more than one way. Addresses no block covers read 0 and
  ** drop writes. */
  const Y8960OPL_ADPCM_REGION *regions;
  uint32_t region_count;

  /* 07h */
  uint8_t start, rec, memory_data, repeat, sp_off;
  /* 08h */
  uint8_t rom, ram64k;
  uint16_t start_reg, stop_reg, delta_n;
  uint8_t volume; /* 12h */
  uint8_t data;   /* 0Fh, the last value written */

  /* status flags, before the masks of 04h. PCM BSY is not START: it repeats
  ** b7 of the last write to 07h, so it stays up after a one-shot sample ends
  ** and when RESET comes with START. */
  uint8_t eos, buf_rdy, pcm_bsy;

  /* CPU access to the sample memory (07h = 60h / 20h), a byte address */
  uint32_t mem_ptr;
  uint8_t read_delay;

  /* playback: the DELTA-N accumulator, and the nibble position (byte * 2,
  ** + 1 for the lower nibble) */
  uint32_t cnt;
  uint8_t adv;
  uint8_t chon;
  uint8_t restart;
  uint8_t clr_dec;  /* the decoder is held clear */
  uint8_t set_flag; /* EOS rises only when this goes from 0 to 1 */
  uint32_t pos;
  uint8_t cpu_byte;

  /* decoder and interpolator */
  int32_t x, step, x_prev;

  int16_t lpf[Y8960OPL_ADPCM_LPF_TAPS];
  int lpf_pos;
} Y8960OPL_ADPCM;

Y8960OPL_ADPCM *Y8960OPL_ADPCM_new(uint32_t clk);
void Y8960OPL_ADPCM_reset(Y8960OPL_ADPCM *);
void Y8960OPL_ADPCM_delete(Y8960OPL_ADPCM *);
void Y8960OPL_ADPCM_writeReg(Y8960OPL_ADPCM *, uint32_t reg, uint32_t val);
/* 04h without the IRQ reset bit: the masks of EOS (b4) and BUF_RDY (b3) */
void Y8960OPL_ADPCM_writeFlagControl(Y8960OPL_ADPCM *, uint32_t val);
/* reading 0Fh */
uint8_t Y8960OPL_ADPCM_readData(Y8960OPL_ADPCM *);
int16_t Y8960OPL_ADPCM_calc(Y8960OPL_ADPCM *);
/* EOS (b4), BUF_RDY (b3) and PCM BSY (b0), before the masks of 04h */
uint8_t Y8960OPL_ADPCM_status(Y8960OPL_ADPCM *);
/* the IRQ reset of 04h */
void Y8960OPL_ADPCM_resetStatus(Y8960OPL_ADPCM *);
void Y8960OPL_ADPCM_setMemoryMap(Y8960OPL_ADPCM *, const Y8960OPL_ADPCM_REGION *regions, uint32_t count);
#endif
