/*
 * SAV8313(PowerPC) CPU board and AG-HVX200 board header
 */
#ifndef _DEFS_XXXX_H
#define _DEFS_XXXX_H

#define SPD_P2PF_ARCH "SAV8313-K230"

enum SPD_GENERIC_ENUM {
  /* for Power Mode */
  SPD_PM_HIGH   = 0x4d,  /* R:600mA, W:600mA */
  SPD_PM_LOW    = 0x34,  /* R:400mA, W:400mA */
  SPD_PM_NORMAL = 0x00,  /* R:400mA, W:600mA */
};


enum SPD_SAV8313K230_ENUM {
  SPD_N_DEV            = 2,
  SPD_N_CACHE          = 2,
  SPD_PM_LEVEL         = SPD_PM_NORMAL,
  SPD_CACHE_N_BUFFER   = 8, /* 4MB */
};

/* for SAV8313BRB1 board LED */
#define DEBUG_PORT      0xe4000000
#define DEBUG_PORT_BIT  0x04
extern unsigned short *DEBUG_PORT_PTR;


static inline void spd_debug_port(int val)
{
  u16 *c = DEBUG_PORT_PTR;

  if(unlikely(NULL == c)){
    printk(KERN_ERR "[spd]ioremap failed at %s\n", __FUNCTION__);
    return;
  }
  
  if(val){
    iowrite16be((*c & ~DEBUG_PORT_BIT), c);
  } else {
    iowrite16be((*c | DEBUG_PORT_BIT), c);
  }
}

#endif /* _DEFS_XXXX_H */
