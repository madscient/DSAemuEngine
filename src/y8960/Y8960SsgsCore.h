/* emu2149.h */
/*
   Forked from emu2149 v1.42 for the Y8960 cartridge, 2026 by madscient.

   The cartridge's SSGS block is the SSG part of a YMZ705: two YM2149 units,
   each with a 4 bit panpot per channel at its registers 10h-12h. One instance
   of this core is one such unit; the caller splits the 00h-3Fh register space
   between two of them. Every exported symbol is renamed with a Y8960 prefix
   because the unmodified emu2149 is linked into the same binary for the plain
   SSG chip.

   The panpot is applied per channel before the rate converter, so the stereo
   output goes through the same converter as the mono one.
*/
#ifndef _Y8960_SSGS_CORE_H_
#define _Y8960_SSGS_CORE_H_

#include <stdint.h>

#define Y8960SSG_MASK_CH(x) (1<<(x))

#ifdef __cplusplus
extern "C"
{
#endif

  typedef struct __Y8960SSG
  {

    /* Volume Table */
    uint32_t *voltbl;

    uint8_t reg[0x20];
    int32_t out;

    uint32_t clk, rate, base_incr;
    uint8_t quality;
    uint8_t clk_div;

    uint16_t count[3];
    uint8_t volume[3];
    uint16_t freq[3];
    uint8_t edge[3];
    uint8_t tmask[3];
    uint8_t nmask[3];
    uint32_t mask;

    uint32_t base_count;

    uint8_t env_ptr;
    uint8_t env_face;

    uint8_t env_continue;
    uint8_t env_attack;
    uint8_t env_alternate;
    uint8_t env_hold;
    uint8_t env_pause;

    uint16_t env_freq;
    uint32_t env_count;

    uint32_t noise_seed;
    uint8_t noise_scaler;
    uint8_t noise_count;
    uint8_t noise_freq;

    /* rate converter */
    uint32_t realstep;
    uint32_t psgtime;
    uint32_t psgstep;

    uint32_t freq_limit;

    /* I/O Ctrl */
    uint8_t adr;

    /* output of channels */
    int16_t ch_out[3];

    /* panpot register value and the left/right gains it gives */
    uint8_t pan[3];
    float pan_gain[3][2];
    int32_t out_stereo[2];

  } Y8960SSG;

  void Y8960SSG_setQuality (Y8960SSG * psg, uint8_t q);
  void Y8960SSG_setClock(Y8960SSG *psg, uint32_t clk);
  void Y8960SSG_setClockDivider(Y8960SSG *psg, uint8_t enable);
  void Y8960SSG_setRate (Y8960SSG * psg, uint32_t rate);
  Y8960SSG *Y8960SSG_new (uint32_t clk, uint32_t rate);
  void Y8960SSG_reset (Y8960SSG *);
  void Y8960SSG_delete (Y8960SSG *);
  void Y8960SSG_writeReg (Y8960SSG *, uint32_t reg, uint32_t val);
  void Y8960SSG_writeIO (Y8960SSG * psg, uint32_t adr, uint32_t val);
  uint8_t Y8960SSG_readReg (Y8960SSG * psg, uint32_t reg);
  uint8_t Y8960SSG_readIO (Y8960SSG * psg);
  int16_t Y8960SSG_calc (Y8960SSG *);
  /* out[0]: left, out[1]: right */
  void Y8960SSG_calcStereo (Y8960SSG *, int32_t out[2]);
  void Y8960SSG_setVolumeMode (Y8960SSG * psg, int type);
  uint32_t Y8960SSG_setMask (Y8960SSG *, uint32_t mask);
  uint32_t Y8960SSG_toggleMask (Y8960SSG *, uint32_t mask);
    
#ifdef __cplusplus
}
#endif

/* deprecated interfaces */
#define Y8960SSG_set_quality Y8960SSG_setQuality
#define Y8960SSG_set_rate Y8960SSG_setRate
#define Y8960SSG_set_clock Y8960SSG_setClock

#endif
