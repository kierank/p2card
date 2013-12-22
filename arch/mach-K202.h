/*
 * AJ-HPX2000/3000/2700/3700 header
 */
#ifndef _MACH_XXXX_H
#define _MACH_XXXX_H

static inline int hwif_pci_to_id(struct pci_dev *pci)
{
  int devfn;

  if(pci == NULL){
    PALERT("null pointer");
    return -EFAULT;
  }
  devfn = pci->bus->self->devfn;
  
  switch(devfn){
  case 16:
    return 0;
  case 17:
    return 1;
  case 24:
    return 2;
  case 25:
    return 3;
  case 32:
    return 4;
  case 33:
    return 5;
  }
  PERROR("unknown device");
  return -EINVAL;
}


#if defined(CONFIG_ZION)
extern int ZION_pci_cache_clear(void);
static inline void hwif_adjust_pci(spd_dev_t *dev, int io_dir, int target)
{
  if(io_dir == SPD_DIR_WRITE){
    spd_out8(dev, SPD_REG_BURST_LENGTH, 0x20);
    ZION_pci_cache_clear();
  } else {
    if(target == SPD_TARGET_ZION){
      spd_out8(dev, SPD_REG_BURST_LENGTH, 0x20);
    } else {
      spd_out8(dev, SPD_REG_BURST_LENGTH, 0x08);
    }
  }
}


static inline int hwif_target_of(u32 bus_addr)
{
  if((bus_addr&0xf0000000) != 0){
    return SPD_TARGET_ZION;
  }
  return SPD_TARGET_LOCAL;
}

#else /* ! CONFIG_ZION */

static inline void hwif_adjust_pci(spd_dev_t *dev, int io_dir, int target)
{
  if(io_dir == SPD_DIR_WRITE){
    spd_out8(dev, SPD_REG_BURST_LENGTH, 0x20);
  } else {
    spd_out8(dev, SPD_REG_BURST_LENGTH, 0x08);
  }
}


static inline int hwif_target_of(u32 bus_addr)
{
  return SPD_TARGET_LOCAL;
}
#endif /* CONFIG_ZION */


static inline void hwif_adjust_latency_timer(spd_dev_t *dev)
{
}


static inline u32 hwif_get_sys_ru(spd_dev_t *dev)
{
  return (SPD_WSIZE_64K);
}


static inline u32 hwif_get_usr_ru(spd_dev_t *dev)
{
  return (SPD_WSIZE_4M);
}

#endif /* _MACH_XXXX_H */
