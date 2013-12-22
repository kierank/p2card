/*
 * AJ-HPD2500(K286) board header
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
  case 144:
    return 0;
  case 145:
    return 1;
  case 152:
    return 2;
  case 153:
    return 3;
  case 160:
    return 4;
  case 161:
    return 5;
  }
  PERROR("unknown device");
  return -EINVAL;
}


#if defined(CONFIG_ZION)
extern int ZION_pci_cache_clear(void);
#else /* !CONFIG_ZION */
# define ZION_pci_cache_clear()
#endif /* CONFIG_ZION */


static inline void hwif_adjust_pci(spd_dev_t *dev, int io_dir, int target)
{
  if((dev->spec_version&0xf0) < 0x30)
    spd_out8(dev, SPD_REG_BURST_LENGTH, 0x20);
  else
    spd_out8(dev, SPD_REG_BURST_LENGTH, 0x80);

  if(io_dir == SPD_DIR_WRITE){
    ZION_pci_cache_clear();
  }
}


static inline int hwif_target_of(u32 bus_addr)
{
  return SPD_TARGET_LOCAL;
}


static inline void hwif_adjust_latency_timer(spd_dev_t *dev)
{
  /* for Ricoh812 on MPC83xx tuning value */
  spd_pci_write_config_byte(dev, SPD_REG_LATENCY_TIMER, 0x80);
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
