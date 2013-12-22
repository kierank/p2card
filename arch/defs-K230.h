/*
 * HVX200 board header
 */
#ifndef _DEFS_XXXX_H
#define _DEFS_XXXX_H

#define SPD_P2PF_ARCH "K230"

enum SPD_GENERIC_ENUM {
  /* for Power Mode */
  SPD_PM_HIGH   = 0x4d,  /* R:600mA, W:600mA */
  SPD_PM_LOW    = 0x34,  /* R:400mA, W:400mA */
  SPD_PM_NORMAL = 0x00,  /* R:400mA, W:600mA */
};


enum SPD_K230_ENUM {
  SPD_N_DEV            = 2,
  SPD_N_CACHE          = 2,
  SPD_PM_LEVEL         = SPD_PM_NORMAL,
  SPD_CACHE_N_BUFFER   = 8, /* 4MB */
};


/* for SH4 CPU board LED */
#define DEBUG_PORT      *((u8 *)0xba000000)
#define DEBUG_PORT_BIT  0x80

/* for K230 Eva board LED */
/*
#define DEBUG_PORT      *((u8 *)0xb9c00000)
#define DEBUG_PORT_BIT  0x01
*/

static inline void spd_debug_port(int val)
{
  u8 c;
  c = DEBUG_PORT;
  if(val){
    c |= DEBUG_PORT_BIT;
  } else {
    c &= ~DEBUG_PORT_BIT;
  }
  DEBUG_PORT = c;
}

#endif /* _DEFS_XXXX_H */
