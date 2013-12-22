/*
 * SAV8315(PowerPC) CPU board and MPC8315ERDB
 */
#ifndef _MACH_XXXX_H
#define _MACH_XXXX_H

static inline int hwif_pci_to_id(struct pci_dev *pci)
{
  int devfn;
  int id;
  static int max_id = 0;
  int bus_no;
  spd_dev_t *dev;

  if(pci == NULL){
    PALERT("null pointer");
    return -EFAULT;
  }
  devfn = pci->bus->self->devfn;

#if 0
  switch(devfn){
  case 144:
    return 0;
  case 145:
    return 0;
  case 152:
    return 0;
  case 153:
    return 0;
  case 160:
    return 0;
  case 161:
    return 0;
  }
  PERROR("unknown device");
  return -EINVAL;
#else /* if 0 */
  bus_no = pci->bus->number;

  for(id = 0; id < max_id; id++){
    dev = &spd_dev[id];
    if(dev->hwif->bus_no == bus_no && dev->hwif->devfn  == devfn){
      return id;
    }
  }
  if(id > SPD_N_DEV){
    PERROR("too many devices(bus_no=%d, devfn=%d, id=%d)", bus_no, devfn, id);
    return -EINVAL;
  }
  max_id = id;
  return id;
#endif /* if 0 */
}


#if defined(DBG_SET_PARAMS)
/* for Ricoh812 on MPC83xx tuning value */
unsigned int bl2 = 0x80; /* for main memory */
unsigned int lt  = 0x80;
module_param(bl2, uint, S_IRUGO|S_IWUSR);
module_param(lt , uint, S_IRUGO|S_IWUSR);

static inline void hwif_adjust_pci(spd_dev_t *dev, int io_dir, int target)
{
  PDEBUG( "Burst Length=0x%X\n", bl2 );
  spd_out8(dev, SPD_REG_BURST_LENGTH, bl2);
}


static inline int hwif_target_of(u32 bus_addr)
{
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
}


static inline int hwif_target_of(u32 bus_addr)
{
  return SPD_TARGET_LOCAL;
}


static inline void hwif_adjust_latency_timer(spd_dev_t *dev)
{
  /* for Ricoh812 on MPC8313 tuning value */
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
