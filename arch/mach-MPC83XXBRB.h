/*
 * MPC83xx(PowerPC) BRB header
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

  /* Ricoh812 x1 on PCI slot */
  switch(devfn){
  case 121:
    return 0;
  case 120:
    return 1;
  }
  PERROR("unknown device");
  return -EINVAL;
}

#if defined(CONFIG_ZION)
extern int ZION_pci_cache_clear(void);
#else /* !CONFIG_ZION */
# define ZION_pci_cache_clear()
#endif /* CONFIG_ZION */


#if defined(DBG_SET_PARAMS)
/* for Ricoh812 on MPC83xx tuning value */
unsigned int bl1 = 0x20; /* for ZION */
unsigned int bl2 = 0x80; /* for others */
unsigned int lt  = 0x80;
module_param(bl1, uint, S_IRUGO|S_IWUSR);
module_param(bl2, uint, S_IRUGO|S_IWUSR);
module_param(lt , uint, S_IRUGO|S_IWUSR);

static inline void hwif_adjust_pci(spd_dev_t *dev, int io_dir, int target)
{
  if(target == SPD_TARGET_ZION){
    PDEBUG( "Burst Length=0x%X\n", bl1 );
    spd_out8(dev, SPD_REG_BURST_LENGTH, bl1);
  } else {
    PDEBUG( "Burst Length=0x%X\n", bl2 );
    spd_out8(dev, SPD_REG_BURST_LENGTH, bl2);
  }

  if(io_dir == SPD_DIR_WRITE){
    ZION_pci_cache_clear();
  }
}


static inline int hwif_target_of(u32 bus_addr)
{
  if((bus_addr&0x80000000) != 0){
    return SPD_TARGET_ZION;
  }
  return SPD_TARGET_LOCAL;
}


static inline void hwif_adjust_latency_timer(spd_dev_t *dev)
{
  PDEBUG( "Latency Timer=0x%X\n", lt );
  spd_pci_write_config_byte(dev, SPD_REG_LATENCY_TIMER, lt);
}

#else /* ! DBG_SET_PARAMS */

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
#endif /* DBG_SET_PARAMS */


static inline u32 hwif_get_sys_ru(spd_dev_t *dev)
{
  return (SPD_WSIZE_64K);
}


static inline u32 hwif_get_usr_ru(spd_dev_t *dev)
{
  return (SPD_WSIZE_4M);
}

#endif /* _MACH_XXXX_H */
