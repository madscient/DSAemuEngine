/*
   Forked from emu8950 v1.1.4 for the Y8960 cartridge, 2026 by madscient.

   The cartridge's OPL2EX block is a YM3812 with the Y8950's ADPCM-B, laid out
   at the Y8950's register addresses. emu8950 already has both halves but
   gives the ADPCM only to its Y8950 and the waveform select only to its
   YM3812, so this fork drops the chip type and keeps both. Every exported
   symbol is renamed with a Y8960 prefix because the unmodified emu8950 is
   linked into the same binary for the Y8950 / OPL / OPL2 chips.

   The ADPCM has no sample ROM, so bit 0 of register 08h is ignored, and its
   sample RAM is a window handed in from outside (Y8960OPL_setADPCMMemory),
   because the cartridge's two circuits divide one 256KB SRAM between them.
*/
#ifndef _Y8960_OPL2EX_CORE_H_
#define _Y8960_OPL2EX_CORE_H_

#include "Y8960Opl2exAdpcm.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define Y8960OPL_DEBUG 0

/* voice data */
typedef struct __Y8960OPL_PATCH {
  uint8_t TL, FB, EG, ML, AR, DR, SL, RR, KR, KL, AM, PM, WS;
} Y8960OPL_PATCH;

/* mask */
#define Y8960OPL_MASK_CH(x) (1 << (x))
#define Y8960OPL_MASK_HH (1 << 9)
#define Y8960OPL_MASK_CYM (1 << 10)
#define Y8960OPL_MASK_TOM (1 << 11)
#define Y8960OPL_MASK_SD (1 << 12)
#define Y8960OPL_MASK_BD (1 << 13)
#define Y8960OPL_MASK_ADPCM (1 << 14)
#define Y8960OPL_MASK_RHYTHM (Y8960OPL_MASK_HH | Y8960OPL_MASK_CYM | Y8960OPL_MASK_TOM | Y8960OPL_MASK_SD | Y8960OPL_MASK_BD)

/* rate conveter */
typedef struct __Y8960OPL_RateConv {
  int ch;
  double timer;
  double f_ratio;
  int16_t *sinc_table;
  int16_t **buf;
} Y8960OPL_RateConv;

Y8960OPL_RateConv *Y8960OPL_RateConv_new(double f_inp, double f_out, int ch);
void Y8960OPL_RateConv_reset(Y8960OPL_RateConv *conv);
void Y8960OPL_RateConv_putData(Y8960OPL_RateConv *conv, int ch, int16_t data);
int16_t Y8960OPL_RateConv_getData(Y8960OPL_RateConv *conv, int ch);
void Y8960OPL_RateConv_delete(Y8960OPL_RateConv *conv);

/* slot */
typedef struct __Y8960OPL_SLOT {
  uint8_t number;

  /* type flags:
   * 000000SM 
   *       |+-- M: 0:modulator 1:carrier
   *       +--- S: 0:normal 1:single slot mode (sd, tom, hh or cym) 
   */
  uint8_t type;

  Y8960OPL_PATCH __patch;  
  Y8960OPL_PATCH *patch;  /* = alias for __patch */

  /* slot output */
  int32_t output[2]; /* output value, latest and previous. */

  /* phase generator (pg) */
  uint16_t *wave_table; /* wave table */
  uint32_t pg_phase;    /* pg phase */
  uint32_t pg_out;      /* pg output, as index of wave table */
  uint8_t pg_keep;      /* if 1, pg_phase is preserved when key-on */
  uint16_t blk_fnum;    /* (block << 9) | f-number */
  uint16_t fnum;        /* f-number (9 bits) */
  uint8_t blk;          /* block (3 bits) */

  /* envelope generator (eg) */
  uint8_t eg_state;         /* current state */
  uint16_t tll;             /* total level + key scale level*/
  uint8_t rks;              /* key scale offset (rks) for eg speed */
  uint8_t eg_rate_h;        /* eg speed rate high 4bits */
  uint8_t eg_rate_l;        /* eg speed rate low 2bits */
  uint32_t eg_shift;        /* shift for eg global counter, controls envelope speed */
  int16_t eg_out;           /* eg output */

  uint32_t update_requests; /* flags to debounce update */

#if Y8960OPL_DEBUG
  uint8_t last_eg_state;
#endif
} Y8960OPL_SLOT;

typedef struct __Y8960OPL {
  Y8960OPL_ADPCM *adpcm;
  uint32_t clk;
  uint32_t rate;

  uint32_t adr;

  uint8_t csm_mode;
  uint8_t csm_key_count;
  uint8_t notesel;

  uint32_t inp_step;
  uint32_t out_step;
  uint32_t out_time;

  uint8_t reg[0x100];
  uint8_t test_flag;
  uint32_t slot_key_status;
  uint8_t rhythm_mode;

  uint32_t eg_counter;

  uint32_t pm_phase;
  uint32_t pm_dphase;

  int32_t am_phase;
  int32_t am_dphase;
  uint8_t lfo_am;

  uint32_t noise;
  uint8_t short_noise;

  Y8960OPL_SLOT slot[18];
  uint8_t ch_alg[9]; // alg for each channels

  uint8_t pan[16];
  float pan_fine[16][2];

  uint32_t mask;
  uint8_t am_mode;
  uint8_t pm_mode;

  /* channel output */
  /* 0..8:tone 9:bd 10:hh 11:sd 12:tom 13:cym 14:adpcm */
  int16_t ch_out[15];

  int16_t mix_out[2];

  Y8960OPL_RateConv *conv;

  uint32_t timer1_counter; //  80us counter
  uint32_t timer2_counter; // 320us counter
  void *timer1_user_data;
  void *timer2_user_data;
  void (*timer1_func)(void *user);
  void (*timer2_func)(void *user);
  uint8_t status;

} Y8960OPL;

Y8960OPL *Y8960OPL_new(uint32_t clk, uint32_t rate);
void Y8960OPL_delete(Y8960OPL *);

void Y8960OPL_reset(Y8960OPL *);

/** 
 * Set output wave sampling rate. 
 * @param rate sampling rate. If clock / 72 (typically 49716 or 49715 at 3.58MHz) is set, the internal rate converter is disabled.
 */
void Y8960OPL_setRate(Y8960OPL *opl, uint32_t rate);

/** 
 * Set internal calcuration quality. Currently no effects, just for compatibility.
 * >= v1.0.0 always synthesizes internal output at clock/72 Hz.
 */
void Y8960OPL_setQuality(Y8960OPL *opl, uint8_t q);

/** 
 * Set pan pot (extra function - not YM2413 chip feature)
 * @param ch 0..8:tone 9:bd 10:hh 11:sd 12:tom 13:cym 14,15:reserved
 * @param pan 0:mute 1:right 2:left 3:center 
 * ```
 * pan: 76543210
 *            |+- bit 1: enable Left output
 *            +-- bit 0: enable Right output
 * ```
 */
void Y8960OPL_setPan(Y8960OPL *opl, uint32_t ch, uint8_t pan);

/**
 * Set fine-grained panning
 * @param ch 0..8:tone 9:bd 10:hh 11:sd 12:tom 13:cym 14,15:reserved
 * @param pan output strength of left/right channel. 
 *            pan[0]: left, pan[1]: right. pan[0]=pan[1]=1.0f for center.
 */
void Y8960OPL_setPanFine(Y8960OPL *opl, uint32_t ch, float pan[2]);

void Y8960OPL_writeIO(Y8960OPL *opl, uint32_t reg, uint8_t val);
void Y8960OPL_writeReg(Y8960OPL *opl, uint32_t reg, uint8_t val);

/**
 * Calculate sample
 */
int16_t Y8960OPL_calc(Y8960OPL *opl);

/**
 * Calulate stereo sample
 */
void Y8960OPL_calcStereo(Y8960OPL *opl, int32_t out[2]);

/** 
 *  Set channel mask 
 *  @param mask mask flag: Y8960OPL_MASK_* can be used.
 *  - bit 0..8: mask for ch 1 to 9 (Y8960OPL_MASK_CH(i))
 *  - bit 9: mask for Hi-Hat (Y8960OPL_MASK_HH)
 *  - bit 10: mask for Top-Cym (Y8960OPL_MASK_CYM)
 *  - bit 11: mask for Tom (Y8960OPL_MASK_TOM)
 *  - bit 12: mask for Snare Drum (Y8960OPL_MASK_SD)
 *  - bit 13: mask for Bass Drum (Y8960OPL_MASK_BD)
 */
uint32_t Y8960OPL_setMask(Y8960OPL *, uint32_t mask);

/**
 * Toggler channel mask flag
 */
uint32_t Y8960OPL_toggleMask(Y8960OPL *, uint32_t mask);

uint8_t Y8960OPL_readIO(Y8960OPL *opl);

/**
 * Read OPL status register
 * @returns
 * 76543210
 * |||||  +- D0: PCM/BSY
 * ||||+---- D3: BUF/RDY
 * |||+----- D4: EOS
 * ||+------ D5: TIMER2
 * |+------- D6: TIMER1
 * +-------- D7: IRQ
 */
uint8_t Y8960OPL_status(Y8960OPL *opl);

/**
 * Attach the ADPCM sample RAM window. The caller owns the memory and must keep
 * it alive while the chip runs; until this is called the ADPCM reads silence.
 */
void Y8960OPL_setADPCMMemory(Y8960OPL *opl, uint8_t *ram, uint32_t size);

/**
 * Copy data into the attached window, clipped at its end.
 */
void Y8960OPL_writeADPCMData(Y8960OPL *opl, uint32_t start, uint32_t length, const uint8_t *data);

/* for compatibility */
#define Y8960OPL_set_rate Y8960OPL_setRate
#define Y8960OPL_set_quality Y8960OPL_setQuality
#define Y8960OPL_set_pan Y8960OPL_setPan
#define Y8960OPL_set_pan_fine Y8960OPL_setPanFine
#define Y8960OPL_calc_stereo Y8960OPL_calcStereo

#ifdef __cplusplus
}
#endif

#endif
