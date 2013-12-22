/*
 * AJ-PCD2 board header
 */
#ifndef _DEFS_XXXX_H
#define _DEFS_XXXX_H

#define SPD_P2PF_ARCH "E605"
#define SPD_OPTION_VER "LowPow"

enum SPD_GENERIC_ENUM {
  /* for Power Mode */
  SPD_PM_HIGH   = 0x4d,  /* R:600mA, W:600mA */
  SPD_PM_LOW    = 0x34,  /* R:400mA, W:400mA */
  SPD_PM_NORMAL = 0x00,  /* R:400mA, W:600mA */
};


enum SPD_E605_ENUM {
  SPD_N_DEV            = 1,
  SPD_N_CACHE          = 1,
  SPD_PM_LEVEL         = SPD_PM_LOW,
  SPD_CACHE_N_BUFFER   = 8, /* 4MB */
};


#endif /* _DEFS_XXXX_H */
