/*
 * P2 Store header
 */
#ifndef _DEFS_XXXX_H
#define _DEFS_XXXX_H

#define SPD_P2PF_ARCH "E522"

enum SPD_GENERIC_ENUM {
  /* for Power Mode */
  SPD_PM_HIGH   = 0x4d,  /* R:600mA, W:600mA */
  SPD_PM_LOW    = 0x34,  /* R:400mA, W:400mA */
  SPD_PM_NORMAL = 0x00,  /* R:400mA, W:600mA */
};


enum SPD_E522_ENUM {
  SPD_N_DEV            = 1,
  SPD_N_CACHE          = 1,
  SPD_PM_LEVEL         = SPD_PM_NORMAL,
  SPD_CACHE_N_BUFFER   = 8, /* 4MB */
};


#ifdef virt_to_bus
# undef virt_to_bus
#endif /* virt_to_bus */

static __inline__ unsigned long virt_to_bus2(volatile void * address)
{
  if (((unsigned long)address >= 0xfa000000) &&
      ((unsigned long)address < 0xffffffff))
    return ((unsigned long)(address));
  else
    return (((unsigned long)(address)) & 0x1fffffff);
}
#define virt_to_bus virt_to_bus2

#endif /* _DEFS_XXXX_H */
