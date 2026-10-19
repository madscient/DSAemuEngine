/* emu2212.h */
/*
   Forked from emu2212 v1.16 for the Y8960 cartridge, 2026 by madscient.

   Each of the five channels of the cartridge's SCC has a stereo position on
   top of the SCC's own registers. The caller hands the position in through
   Y8960SCC_setChannelPan; the core takes no register for it. The position law
   is in Y8960Pan.h. Every exported symbol is renamed with a Y8960 prefix
   because the unmodified emu2212 is linked into the same binary for the
   plain SCC chips.

   Y8960SCC_calcStereo applies the position per channel to the same channel
   outputs that Y8960SCC_calc sums. Y8960SCC_calc (mono) does not apply it.
*/
#ifndef _Y8960_SCC_CORE_H_
#define _Y8960_SCC_CORE_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define Y8960SCC_STANDARD 0
#define Y8960SCC_ENHANCED 1

#define Y8960SCC_MASK_CH(x) (1<<(x))

typedef struct __Y8960SCC {

  uint32_t clk, rate ,base_incr, quality ;

  int16_t out;
  uint32_t type ;
  uint32_t mode ;
  uint32_t active;
  uint32_t base_adr;
  uint32_t mask ;
  
  uint32_t realstep ;
  uint32_t scctime ;
  uint32_t sccstep ;

  uint32_t incr[5] ;

  int8_t  wave[5][32] ;

  uint32_t count[5] ;
  uint32_t freq[5] ;
  uint32_t phase[5] ;
  uint32_t volume[5] ;
  uint32_t offset[5] ;
  uint8_t reg[0x100-0xC0];

  int ch_enable ;
  int ch_enable_next ;

  int cycle_4bit ;
  int cycle_8bit ;
  int refresh ;
  int rotate[5] ;

  int16_t ch_out[5] ;

  /* stereo position of each channel, and the left/right gains it gives */
  uint8_t pan[5] ;
  float ch_gain[5][2] ;

} Y8960SCC ;


Y8960SCC *Y8960SCC_new(uint32_t c, uint32_t r) ;
void Y8960SCC_reset(Y8960SCC *scc) ;
void Y8960SCC_set_rate(Y8960SCC *scc, uint32_t r);
void Y8960SCC_set_quality(Y8960SCC *scc, uint32_t q) ;
void Y8960SCC_set_type(Y8960SCC *scc, uint32_t type) ;
void Y8960SCC_delete(Y8960SCC *scc) ;
int16_t Y8960SCC_calc(Y8960SCC *scc) ;
/* out[0]: left, out[1]: right */
void Y8960SCC_calcStereo(Y8960SCC *scc, int32_t out[2]) ;
/* ch: 0-4. pan: 1 hard left, 128 centre, 255 hard right. 128 after reset. */
void Y8960SCC_setChannelPan(Y8960SCC *scc, uint32_t ch, uint8_t pan) ;
void Y8960SCC_write(Y8960SCC *scc, uint32_t adr, uint32_t val) ;
void Y8960SCC_writeReg(Y8960SCC *scc, uint32_t adr, uint32_t val) ;
uint32_t Y8960SCC_read(Y8960SCC *scc, uint32_t adr) ;
uint32_t Y8960SCC_setMask(Y8960SCC *scc, uint32_t adr) ;
uint32_t Y8960SCC_toggleMask(Y8960SCC *scc, uint32_t adr) ;

#ifdef __cplusplus
}
#endif

#endif
