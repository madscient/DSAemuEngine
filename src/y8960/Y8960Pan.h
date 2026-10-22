/*
   The 8 bit stereo position the Y8960 gives the SSGEX channels (28h-2Eh), the
   OPLLEX channels (50h-58h), the OPL2EX channels and ADPCM (F6h-FFh) and the
   SCC channels (9900h-9904h), 2026 by madscient.

   The specification fixes only the ends and the centre: 1 is hard left, 128
   the centre, 255 hard right, and 128 after reset. The law in between is equal
   power, and 0, which the specification leaves out, is the centre. Both are
   this emulator's choice, not the hardware's.
*/
#ifndef _Y8960_PAN_H_
#define _Y8960_PAN_H_

#include <math.h>
#include <stdint.h>

#define Y8960_PAN_CENTER 128

/* gain[0]: left, gain[1]: right */
static inline void Y8960_panGain(uint8_t pan, double gain[2]) {
  const double quarter = 1.57079632679489661923; /* pi / 2 */
  double theta;

  if (pan == 0)
    pan = Y8960_PAN_CENTER;
  theta = (double)(pan - 1) / 254.0 * quarter;
  gain[0] = cos(theta);
  gain[1] = sin(theta);
}

#endif
