/*
 P2card low-level hardware private header
 $Id: spd_hwif.h 230 2006-09-27 09:26:53Z hiraoka $
 */
#ifndef _SPD_HWIF_H
#define _SPD_HWIF_H

enum SPD_PCI_ENUM {
  SPD_PCI_VENDOR_ID       = 0x10f7,
  SPD_PCI_DEVICE_ID       = 0x8206,
  SPD_ADPT_PCI_DEVICE_ID  = 0x820e,

  SPD_REG_CACHE_LINE_SIZE = 0x0c,
};


enum SPD_REG_ENUM {
  /* P2card Register */
  SPD_REG_ISR   = 0x00,
  SPD_REG_INT   = 0x01,
  SPD_REG_DATA	= 0x10,
  SPD_REG_FTR   = 0x11,
  SPD_REG_ERR   = 0x11,
  SPD_REG_SCR   = 0x12,
  SPD_REG_SNR   = 0x13,
  SPD_REG_CLR   = 0x14,
  SPD_REG_BLR   = 0x14,
  SPD_REG_CHR   = 0x15,
  SPD_REG_BHR   = 0x15,
  SPD_REG_DHR   = 0x16,
  SPD_REG_CMD   = 0x17,
  SPD_REG_STR   = 0x17,
  SPD_REG_ASR   = 0x1e,
  SPD_REG_DCR   = 0x1e,
  SPD_REG_CT3   = 0x1d,

  SPD_REG_HOST_ADDRESS  = 0x08,
  SPD_REG_BURST_LENGTH  = 0x0c,
  SPD_REG_LATENCY_TIMER = 0x0d,
  
  /* SPD_REG_ISR */
  SPD_BIT_IRQ   = 0x04,
  
  /* SPD_REG_INT */
  SPD_BIT_IRQE	= 0x04,
  
  /* SPD_REG_STR */
  SPD_BIT_BSY   = 0x80,
  SPD_BIT_RDY   = 0x40,
  SPD_BIT_DSC   = 0x10,
  SPD_BIT_DRQ   = 0x08,
  SPD_BIT_ABRT  = 0x04,
  SPD_BIT_ERR   = 0x01,
  
};


enum SPD_CMD_ENUM {
  /* SPD_REG_CMD */
  SPD_CMD_IDENTIFY_DEVICE      = 0xec,
  SPD_CMD_SET_FEATURE          = 0xef,
  SPD_CMD_SET_POWER_MODE       = 0x05,
  SPD_CMD_SET_DISABLE_CAM_TRNS = 0x04,
  SPD_CMD_SEQ_WRITE_SECTOR_DMA = 0xfa,
  SPD_CMD_READ_SECTOR_DMA      = 0xfb,
  SPD_CMD_WRITE_SECTOR_DMA     = 0xfc,
  SPD_CMD_PIO_NODATA           = 0xfd,
  SPD_CMD_BLOCK_ERASE          = 0x03,
  SPD_CMD_SECTOR_ERASE         = 0x02,
  SPD_CMD_AU_ERASE             = 0x07,
  SPD_CMD_INTERFACE_CONDITION  = 0x10,
  SPD_CMD_CARD_INITIALIZE      = 0x30,
  SPD_CMD_CARD_RESCUE          = 0x31,
  SPD_CMD_TABLE_RECOVER        = 0x32,
  SPD_CMD_SECURED_REC          = 0x40,
  SPD_CMD_START_REC            = 0x50,
  SPD_CMD_CREATE_DIR           = 0x51,
  SPD_CMD_UPDATE_CI            = 0x52,
  SPD_CMD_SET_NEW_AU           = 0x53,
  SPD_CMD_END_REC              = 0x54,
  SPD_CMD_ESW                  = 0x60,
  SPD_CMD_GO_HIBERNATE         = 0x80,
  SPD_CMD_SD_DEV_RESET         = 0xf0,
  SPD_CMD_PIO_DATAIN           = 0xfe,
  SPD_CMD_LOG_SENSE            = 0x01,
  SPD_CMD_GET_SD_STA           = 0x02,
  SPD_CMD_GET_LINFO            = 0x03,
  SPD_CMD_GET_DINFO            = 0x04,
  SPD_CMD_GET_PH               = 0x05,
  SPD_CMD_GET_PHS              = 0x06,
  SPD_CMD_PIO_DATAOUT          = 0xff,
  SPD_CMD_LOG_WRITE            = 0x01,
  SPD_CMD_FIRMWARE_UPDATE      = 0x02,
  SPD_CMD_LMG                  = 0x03,
  SPD_CMD_DMG                  = 0x04,
  SPD_CMD_DAU                  = 0x05,
  SPD_CMD_SET_DPARAM           = 0x06,
};


typedef struct _spd_command_t {
  u8 feature;
  u8 sec_cnt;
  u8 sec_num;
  u8 cyl_low;
  u8 cyl_high;
  u8 drv_headnum;
  u8 command;
} spd_command_t;


typedef struct _spd_hwif_private_t {
  u32 base_address;
  u32 address_range;
  struct pci_dev *pci;
  int bus_no;
  int devfn;
  int irq_number;
  int request_irq;
  struct timer_list timer;
  spd_command_t cmd;
} spd_hwif_t;


/* spd_hwif.c */
int spd_hwif_init(void);
int spd_hwif_exit(void);


#if defined(SPD_USE_MMIO)
enum SPD_IO_ENUM {
  SPD_IO_REG_OFFSET = 0x800,
  SPD_IO_BAR        = 1,
};


static inline u32 spd_ioremap(spd_dev_t *dev)
{
  return (u32)ioremap(pci_resource_start(dev->hwif->pci, SPD_IO_BAR),
		      pci_resource_len(dev->hwif->pci, SPD_IO_BAR));
}


static inline void spd_iounmap(spd_dev_t *dev)
{
  iounmap((void *)dev->hwif->base_address);
}


static inline u32 spd_resource_len(spd_dev_t *dev)
{
  return pci_resource_len(dev->hwif->pci, SPD_IO_BAR);
}


static inline u8 spd_in8(spd_dev_t *dev, int port)
{
  return ioread8((void *)dev->hwif->base_address+port+SPD_IO_REG_OFFSET);
}


static inline u16 spd_in16(spd_dev_t *dev, int port)
{
  return ioread16((void *)dev->hwif->base_address+port+SPD_IO_REG_OFFSET);
}


static inline u32 spd_in32(spd_dev_t *dev, int port)
{
  return ioread32((void *)dev->hwif->base_address+port+SPD_IO_REG_OFFSET);
}


static inline void spd_out8(spd_dev_t *dev, int port, u8 val)
{
  iowrite8(val, (void *)dev->hwif->base_address+port+SPD_IO_REG_OFFSET);
}


static inline void spd_out16(spd_dev_t *dev, int port, u16 val)
{
  iowrite16(val, (void *)dev->hwif->base_address+port+SPD_IO_REG_OFFSET);
}


static inline void spd_out32(spd_dev_t *dev, int port, u32 val)
{
  iowrite32(val, (void *)dev->hwif->base_address+port+SPD_IO_REG_OFFSET);
}


#else /* !SPD_USE_MMIO */

enum SPD_IO_ENUM {
  SPD_IO_REG_OFFSET = 0,
  SPD_IO_BAR        = 0,
};


static inline u32 spd_ioremap(spd_dev_t *dev)
{
  return pci_resource_start(dev->hwif->pci, SPD_IO_BAR);
}


static inline void spd_iounmap(spd_dev_t *dev)
{
}


static inline u32 spd_resource_len(spd_dev_t *dev)
{
  return pci_resource_len(dev->hwif->pci, SPD_IO_BAR);
}


static inline u8 spd_in8(spd_dev_t *dev, int port)
{
  return inb(dev->hwif->base_address+port+SPD_IO_REG_OFFSET);
}


static inline u16 spd_in16(spd_dev_t *dev, int port)
{
  return inw(dev->hwif->base_address+port+SPD_IO_REG_OFFSET);
}


static inline u32 spd_in32(spd_dev_t *dev, int port)
{
  return inl(dev->hwif->base_address+port+SPD_IO_REG_OFFSET);
}


static inline void spd_out8(spd_dev_t *dev, int port, u8 val)
{
  outb(val, dev->hwif->base_address+port+SPD_IO_REG_OFFSET);
}


static inline void spd_out16(spd_dev_t *dev, int port, u16 val)
{
  outw(val, dev->hwif->base_address+port+SPD_IO_REG_OFFSET);
}


static inline void spd_out32(spd_dev_t *dev, int port, u32 val)
{
  outl(val, dev->hwif->base_address+port+SPD_IO_REG_OFFSET);
}
#endif /* SPD_USE_MMIO */


static inline int spd_pci_write_config_dword(spd_dev_t *dev,
					     int addr, u32 value)
{ 
  struct pci_dev *pci;
  unsigned long flags = 0;

  spin_lock_irqsave(&dev->lock, flags);
  pci = dev->hwif->pci;
  if(unlikely(pci == NULL)){
    spin_unlock_irqrestore(&dev->lock, flags);
    return -ENODEV;
  }
  pci_write_config_dword(pci, addr, value);
  spin_unlock_irqrestore(&dev->lock, flags);

  return 0;
}


static inline int spd_pci_read_config_dword(spd_dev_t *dev,
					    int addr, u32 *value)
{ 
  struct pci_dev *pci;
  unsigned long flags = 0;

  spin_lock_irqsave(&dev->lock, flags);
  pci = dev->hwif->pci;
  if(unlikely(pci == NULL)){
    spin_unlock_irqrestore(&dev->lock, flags);
    return -ENODEV;
  }
  pci_read_config_dword(pci, addr, value);
  spin_unlock_irqrestore(&dev->lock, flags);

  return 0;
}


static inline int spd_pci_read_config_dword_nolock(spd_dev_t *dev,
						   int addr, u32 *value)
{ 
  struct pci_dev *pci = dev->hwif->pci;
  if(unlikely(pci == NULL)){
    return -ENODEV;
  }
  pci_read_config_dword(pci, addr, value);
  return 0;
}


static inline int spd_pci_write_config_byte(spd_dev_t *dev,
					    int addr, u8 value)            
{
  struct pci_dev *pci;
  unsigned long flags = 0;

  spin_lock_irqsave(&dev->lock, flags);
  pci = dev->hwif->pci;
  if(unlikely(pci == NULL)){
    spin_unlock_irqrestore(&dev->lock, flags);
    return -ENODEV;
  }
  pci_write_config_byte(pci, addr, value);
  spin_unlock_irqrestore(&dev->lock, flags);

  return 0;
}


static inline int spd_is_lost(spd_dev_t *dev, int lock)
{
  u32 d = 0;
  int retval = 0;

  if(lock) /* Lock spin lock. */
    retval = spd_pci_read_config_dword(dev, 0, &d);
  else /* NOT lock spin lock. */
    retval = spd_pci_read_config_dword_nolock(dev, 0, &d);

  if(retval < 0){
    return 1;
  }
  return (   d != (SPD_PCI_DEVICE_ID<<16|SPD_PCI_VENDOR_ID)
	  && d != (SPD_ADPT_PCI_DEVICE_ID<<16|SPD_PCI_VENDOR_ID) );
}
#endif /* _SPD_HWIF_H */
