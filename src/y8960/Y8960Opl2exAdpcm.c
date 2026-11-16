/**
 * ADPCM-B of the Y8960 cartridge's OPL2EX block
 */
/*
   Started as a fork of emu8950 v1.1.4 (emuadpcm.c) for the Y8960 cartridge,
   2026 by madscient. Rewritten in 2026 to follow the behaviour of the
   Y8960's RTL (hra1129/Y8960_Cartridge, v0.9.16), which wraps the ADPCM-B of
   JT12 in a Y8950 style register interface. No RTL code is taken over; only
   the API and the list of memory blocks remain from the emu8950 fork.

   What the RTL does and this file follows:
   - 07h: START with MEMORY DATA plays from the sample memory; START without
     it decodes the bytes the CPU writes to 0Fh (synthesis from the CPU),
     without an end address and without EOS. REC with MEMORY DATA writes the
     sample memory through 0Fh, MEMORY DATA alone reads it. RESET clears the
     whole register. SP-OFF mutes the output (the specification marks it as
     abolished; the RTL keeps it).
   - 08h: the start and stop addresses always count 4 bytes. 64K cuts the
     memory address to 16 bits. ROM makes the sample memory read 0 and drop
     writes.
   - Playback runs from the upper nibble of byte start * 4 up to byte
     stop * 4 + 3. Reaching the last nibble raises EOS; a one-shot sample then
     clears START, REC, MEMORY DATA, REPEAT and SP-OFF, a repeating one starts
     over with a cleared decoder. A START written while playing restarts from
     the start address without clearing the decoder, which takes one more
     nibble from the old position first.
   - PCM BSY is the START bit, so it also drops at the end of a one-shot
     sample. The IRQ reset of 04h leaves it alone.
   - 12h is 255 after a reset.
   - The decoder saturates to 16 bits. The output is the linear interpolation
     between the last two decoded values, weighted by the DELTA-N
     accumulator, then the 12h gain, then a fixed low pass filter, then the
     level the RTL gives the ADPCM against the FM.

   Not modelled: the latency of the SRAM (BUF_RDY returns to 1 at once after
   a CPU access) and the pipeline delays of a few samples. REC without
   MEMORY DATA (recording) is not implemented in the RTL either.
*/
#include "Y8960Opl2exAdpcm.h"
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>

#define STEP_MIN 127
#define STEP_MAX 24576

#define DECODE_MAX 32767
#define DECODE_MIN (-32768)

#define CLAP(min, x, max) ((x < min) ? min : (max < x) ? max : x)

/* Bitmask for register $07 */
#define R07_RESET 1
#define R07_SP_OFF 8
#define R07_REPEAT 16
#define R07_MEMORY_DATA 32
#define R07_REC 64
#define R07_START 128

/* Bitmask for register $08 */
#define R08_ROM 1
#define R08_64K 2

/* Bitmask for register $04 */
#define R04_MASK_BUF_RDY 8
#define R04_MASK_EOS 16

/* Bit for status register */
#define STATUS_PCM_BSY 1
#define STATUS_BUF_RDY 8
#define STATUS_EOS 16

/* Low pass filter for the output: 25 taps, linear phase, Kaiser window
** (beta 5), cut off at 5.5 kHz for a sampling rate of clock / 72. The values
** are the windowed sinc rounded to Q15, with the centre tap taking up the
** rounding so that the sum is 32768. Taps 13-24 mirror taps 11-0. */
static const int32_t lpf_coef[13] = {
    28, 78, 96, -7, -283, -645, -825, -458, 716, 2632, 4830, 6591, 7262,
};

Y8960OPL_ADPCM *Y8960OPL_ADPCM_new(uint32_t clk) {
  Y8960OPL_ADPCM *_this;

  _this = (Y8960OPL_ADPCM *)malloc(sizeof(Y8960OPL_ADPCM));
  if (!_this)
    return NULL;

  _this->clk = clk;
  _this->regions = NULL;
  _this->region_count = 0;

  Y8960OPL_ADPCM_reset(_this);

  return _this;
}

void Y8960OPL_ADPCM_delete(Y8960OPL_ADPCM *_this) {
  free(_this);
}

static inline uint8_t read_mem(Y8960OPL_ADPCM *_this, uint32_t addr) {
  uint32_t i;
  for (i = 0; i < _this->region_count; i++) {
    const Y8960OPL_ADPCM_REGION *r = &_this->regions[i];
    if (addr >= r->base && addr - r->base < r->size)
      return r->data[addr - r->base];
  }
  return 0;
}

static inline void write_mem(Y8960OPL_ADPCM *_this, uint32_t addr, uint8_t data) {
  uint32_t i;
  for (i = 0; i < _this->region_count; i++) {
    const Y8960OPL_ADPCM_REGION *r = &_this->regions[i];
    if (addr >= r->base && addr - r->base < r->size) {
      if (r->writable)
        r->data[addr - r->base] = data;
      return;
    }
  }
}

static inline uint32_t memory_address(Y8960OPL_ADPCM *_this, uint32_t addr) {
  return addr & (_this->ram64k ? 0xFFFF : 0x3FFFF);
}

static inline uint32_t start_ptr(Y8960OPL_ADPCM *_this) { return (uint32_t)_this->start_reg << 2; }

static inline uint32_t stop_ptr(Y8960OPL_ADPCM *_this) { return ((uint32_t)_this->stop_reg << 2) | 3; }

static inline int playing(Y8960OPL_ADPCM *_this) { return _this->start && !_this->rec; }

/* BUF_RDY is held at 1 while nothing runs and while the CPU accesses the
** sample memory */
static inline int buf_rdy_held(Y8960OPL_ADPCM *_this) { return !_this->start && (_this->memory_data || !_this->rec); }

static void clear_decoder(Y8960OPL_ADPCM *_this) {
  _this->x = 0;
  _this->step = STEP_MIN;
}

void Y8960OPL_ADPCM_reset(Y8960OPL_ADPCM *_this) {
  int i;

  for (i = 0; i < 0x20; i++)
    _this->reg[i] = 0;
  _this->reg[0x12] = 255;

  _this->start = _this->rec = _this->memory_data = _this->repeat = _this->sp_off = 0;
  _this->rom = _this->ram64k = 0;
  _this->start_reg = _this->stop_reg = _this->delta_n = 0;
  _this->volume = 255;
  _this->data = 0;

  _this->eos = 0;
  _this->buf_rdy = 1;

  _this->mem_ptr = 0;
  _this->read_delay = 0;

  _this->cnt = 0;
  _this->adv = 1;
  _this->chon = 0;
  _this->restart = 0;
  _this->clr_dec = 1;
  _this->set_flag = 0;
  _this->pos = 0;
  _this->cpu_byte = 0;

  clear_decoder(_this);
  _this->x_prev = 0;

  for (i = 0; i < Y8960OPL_ADPCM_LPF_TAPS; i++)
    _this->lpf[i] = 0;
  _this->lpf_pos = 0;
}

static void decode(Y8960OPL_ADPCM *_this, uint32_t val) {
  static const int32_t F[] = {57, 57, 57, 57, 77, 102, 128, 153};
  const int32_t d = ((((val & 7) << 1) | 1) * _this->step) >> 3;
  int32_t x = (val & 8) ? _this->x - d : _this->x + d;

  _this->x = CLAP(DECODE_MIN, x, DECODE_MAX);
  _this->step = CLAP(STEP_MIN, (_this->step * F[val & 7]) >> 6, STEP_MAX);
}

static uint32_t fetch_nibble(Y8960OPL_ADPCM *_this) {
  uint8_t byte;

  if (!_this->memory_data) {
    /* Synthesis from the CPU: 0Fh is taken for the upper nibble and kept for
    ** the lower one, and BUF_RDY asks for the next byte. */
    if (!(_this->pos & 1)) {
      _this->cpu_byte = _this->data;
      _this->buf_rdy = 1;
    }
    byte = _this->cpu_byte;
  } else {
    byte = _this->rom ? 0 : read_mem(_this, memory_address(_this, _this->pos >> 1));
  }
  return (_this->pos & 1) ? (byte & 0x0F) : (byte >> 4);
}

/* EOS comes from an edge: set_flag stays 1 after a sample ends and drops only
** when the address moves on. So after a sample has ended, a start address past
** the stop address raises no EOS, and START (PCM BSY) then stays up. */
static void end_of_sample(Y8960OPL_ADPCM *_this) {
  const int edge = !_this->set_flag;

  _this->set_flag = 1;
  _this->clr_dec = 1;
  clear_decoder(_this);
  if (_this->repeat) {
    _this->restart = 1;
  } else {
    _this->chon = 0;
  }
  if (edge) {
    _this->eos = 1;
    if (!_this->repeat)
      _this->start = _this->rec = _this->memory_data = _this->repeat = _this->sp_off = 0;
  }
}

/* The RTL stops the address counter and clears the decoder as soon as START
** drops or REC rises, so RESET and START written back to back still clear the
** decoder. The DELTA-N accumulator is reset only on the next sample (in
** step_playback). */
static void stop_playback(Y8960OPL_ADPCM *_this) {
  _this->chon = 0;
  _this->restart = 0;
  _this->clr_dec = 1;
  clear_decoder(_this);
}

/* One sample at clock / 72. The address moves on when the DELTA-N
** accumulator carried on the previous sample. */
static void step_playback(Y8960OPL_ADPCM *_this) {
  const uint8_t adv = _this->adv;

  if (playing(_this)) {
    const uint32_t sum = _this->cnt + _this->delta_n;
    _this->adv = (uint8_t)(sum >> 16);
    _this->cnt = sum & 0xFFFF;
  } else {
    _this->cnt = 0;
    _this->adv = 1;
    stop_playback(_this);
    return;
  }

  if (!adv)
    return;

  if (_this->restart) {
    _this->x_prev = _this->x;
    /* A START written while playing does not clear the decoder, which takes
    ** one more nibble from the old position on the way back. */
    if (_this->chon && !_this->clr_dec)
      decode(_this, fetch_nibble(_this));
    _this->pos = (uint32_t)_this->start_reg << 3;
    _this->restart = 0;
    _this->chon = 1;
    _this->clr_dec = 0;
  } else if (_this->chon) {
    _this->x_prev = _this->x;
    if (!_this->memory_data || _this->pos < (((uint32_t)_this->stop_reg << 3) | 7)) {
      decode(_this, fetch_nibble(_this));
      _this->pos++;
      _this->set_flag = 0;
    } else {
      end_of_sample(_this);
    }
  }
}

static int16_t lowpass(Y8960OPL_ADPCM *_this, int16_t in) {
  int32_t acc;
  int k, newest, oldest;

  _this->lpf[_this->lpf_pos] = in;
  newest = _this->lpf_pos;
  _this->lpf_pos = (_this->lpf_pos + 1) % Y8960OPL_ADPCM_LPF_TAPS;
  oldest = _this->lpf_pos;

  acc = 0;
  for (k = 0; k < 12; k++) {
    const int a = (newest - k + Y8960OPL_ADPCM_LPF_TAPS) % Y8960OPL_ADPCM_LPF_TAPS;
    const int b = (oldest + k) % Y8960OPL_ADPCM_LPF_TAPS;
    acc += lpf_coef[k] * (_this->lpf[a] + _this->lpf[b]);
  }
  acc += lpf_coef[12] * _this->lpf[(newest - 12 + Y8960OPL_ADPCM_LPF_TAPS) % Y8960OPL_ADPCM_LPF_TAPS];

  acc = (acc + 16384) >> 15;
  return (int16_t)CLAP(-32768, acc, 32767);
}

int16_t Y8960OPL_ADPCM_calc(Y8960OPL_ADPCM *_this) {
  const uint32_t phase = _this->cnt;
  int32_t inter, gain;

  step_playback(_this);

  if (!_this->chon) {
    _this->x_prev = 0;
    inter = 0;
  } else {
    inter = _this->x_prev + (int32_t)(((int64_t)(_this->x - _this->x_prev) * phase) >> 16);
  }

  gain = (inter * _this->volume) >> 8;
  if (_this->sp_off || !playing(_this))
    gain = 0;

  /* The filter runs at the 16 bit scale of the RTL. The RTL adds the ADPCM
  ** >> 2 to the FM, whose operators span +-4095; the FM here spans half of
  ** that (_MO), hence >> 3: a full scale ADPCM is about twice a melody
  ** channel. */
  return (int16_t)(lowpass(_this, (int16_t)gain) >> 3);
}

void Y8960OPL_ADPCM_writeReg(Y8960OPL_ADPCM *_this, uint32_t adr, uint32_t data) {
  adr &= 0x1f;
  data &= 0xff;

  switch (adr) {
  case 0x07:
    if (data & R07_RESET) {
      _this->start = _this->rec = _this->memory_data = _this->repeat = _this->sp_off = 0;
      _this->mem_ptr = 0;
      stop_playback(_this);
    } else {
      _this->start = (data & R07_START) != 0;
      _this->rec = (data & R07_REC) != 0;
      _this->memory_data = (data & R07_MEMORY_DATA) != 0;
      _this->repeat = (data & R07_REPEAT) != 0;
      _this->sp_off = (data & R07_SP_OFF) != 0;
      if (playing(_this))
        _this->restart = 1;
      else
        stop_playback(_this);
      if (_this->memory_data) {
        _this->mem_ptr = start_ptr(_this);
        _this->read_delay = 2;
        if (!_this->start)
          _this->buf_rdy = 1;
      } else {
        _this->mem_ptr = 0;
      }
    }
    _this->reg[0x07] = (uint8_t)data;
    break;

  case 0x08:
    _this->rom = (data & R08_ROM) != 0;
    _this->ram64k = (data & R08_64K) != 0;
    _this->reg[0x08] = (uint8_t)data;
    break;

  case 0x09:
  case 0x0A:
    _this->reg[adr] = (uint8_t)data;
    _this->start_reg = (uint16_t)((_this->reg[0x0A] << 8) | _this->reg[0x09]);
    break;

  case 0x0B:
  case 0x0C:
    _this->reg[adr] = (uint8_t)data;
    _this->stop_reg = (uint16_t)((_this->reg[0x0C] << 8) | _this->reg[0x0B]);
    break;

  case 0x0F:
    _this->reg[0x0F] = (uint8_t)data;
    _this->data = (uint8_t)data;
    if (!_this->start && _this->rec && _this->memory_data) {
      const uint32_t wp = _this->read_delay ? start_ptr(_this) : _this->mem_ptr;
      _this->read_delay = 0;
      if (wp <= stop_ptr(_this)) {
        if (wp == stop_ptr(_this)) {
          _this->eos = 1;
          _this->mem_ptr = start_ptr(_this);
        } else {
          _this->mem_ptr = wp + 1;
        }
        if (!_this->rom)
          write_mem(_this, memory_address(_this, wp), (uint8_t)data);
        _this->buf_rdy = 1;
      }
    } else if (playing(_this) && !_this->memory_data) {
      _this->buf_rdy = 0;
    }
    break;

  case 0x10:
  case 0x11:
    _this->reg[adr] = (uint8_t)data;
    _this->delta_n = (uint16_t)((_this->reg[0x11] << 8) | _this->reg[0x10]);
    break;

  case 0x12:
    _this->reg[0x12] = (uint8_t)data;
    _this->volume = (uint8_t)data;
    break;

  default:
    break;
  }
}

void Y8960OPL_ADPCM_writeFlagControl(Y8960OPL_ADPCM *_this, uint32_t val) {
  if (val & R04_MASK_EOS)
    _this->eos = 0;
  if (val & R04_MASK_BUF_RDY)
    _this->buf_rdy = (uint8_t)buf_rdy_held(_this);
  else if (buf_rdy_held(_this))
    _this->buf_rdy = 1;
}

uint8_t Y8960OPL_ADPCM_readData(Y8960OPL_ADPCM *_this) {
  uint8_t byte;

  if (_this->start || _this->rec || !_this->memory_data)
    return 0;

  /* the first two reads after 07h are dummies */
  if (_this->read_delay) {
    _this->read_delay--;
    _this->buf_rdy = 1;
    return _this->data;
  }

  if (_this->mem_ptr > stop_ptr(_this)) {
    _this->eos = 1;
    return 0;
  }

  byte = _this->rom ? 0 : read_mem(_this, memory_address(_this, _this->mem_ptr));
  _this->mem_ptr++;
  _this->buf_rdy = 1;
  return byte;
}

uint8_t Y8960OPL_ADPCM_status(Y8960OPL_ADPCM *_this) {
  return (uint8_t)((_this->eos ? STATUS_EOS : 0) | (_this->buf_rdy ? STATUS_BUF_RDY : 0) |
                   (_this->start ? STATUS_PCM_BSY : 0));
}

void Y8960OPL_ADPCM_resetStatus(Y8960OPL_ADPCM *_this) {
  _this->eos = 0;
  _this->buf_rdy = (uint8_t)buf_rdy_held(_this);
}

void Y8960OPL_ADPCM_setMemoryMap(Y8960OPL_ADPCM *_this, const Y8960OPL_ADPCM_REGION *regions, uint32_t count) {
  _this->regions = regions;
  _this->region_count = regions ? count : 0;
}
