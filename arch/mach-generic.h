/*
 * Generic x86 PC board header
 */
#ifndef _MACH_XXXX_H
#define _MACH_XXXX_H

static inline int hwif_pci_to_id(struct pci_dev *pci)
{
  int id;
  static int max_id = 0;
  int devfn;
  int bus_no;
  spd_dev_t *dev;

  if(pci == NULL){
    PALERT("null pointer");
    return -EFAULT;
  }
  devfn  = pci->devfn;
  bus_no = pci->bus->number;

  for(id = 0; id < max_id; id++){
    dev = &spd_dev[id];
    if(dev->hwif->bus_no == bus_no && dev->hwif->devfn  == devfn){
      return id;
    }
  }
  if(id >= SPD_N_DEV){
    PERROR("too many devices(bus_no=%d, devfn=%d)", bus_no, devfn);
    return -EINVAL;
  }
  max_id = id + 1;
  return id;
}


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
  /* Don't care */
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
