/*
 P2card low-level hardware modules
 $Id: spd_hwif.c 21639 2013-04-09 00:15:24Z Yasuyuki Matsumoto $
 */
#define DEVICE_NAME  "spd"

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/hdreg.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/blkpg.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <asm/uaccess.h>
#include "spd.h"
#include "spd_hwif.h"

#if defined(CONFIG_P2PF_K230)
#include "arch/mach-K230.h"
#elif defined(CONFIG_P2PF_K250)
#include "arch/mach-K250.h"
#elif defined(CONFIG_P2PF_K202)
#include "arch/mach-K202.h"
#elif defined(CONFIG_P2PF_K240)
#include "arch/mach-K240.h"
#elif defined(CONFIG_P2PF_K246A)
#include "arch/mach-K246A.h"
#elif defined(CONFIG_P2PF_E51X)
#include "arch/mach-E51X.h"
#elif defined(CONFIG_P2PF_E522)
#include "arch/mach-E522.h"
#elif defined(CONFIG_P2PF_K200)
#include "arch/mach-K200.h"
#elif defined(CONFIG_P2PF_K220)
#include "arch/mach-K220.h"
#elif defined(CONFIG_P2PF_K251)
#include "arch/mach-K251.h"
#elif defined(CONFIG_P2PF_K277)
#include "arch/mach-K277.h"
#elif defined(CONFIG_P2PF_K286)
#include "arch/mach-K286.h"
#elif defined(CONFIG_P2PF_K298)
#include "arch/mach-K298.h"
#elif defined(CONFIG_P2PF_K302)
#include "arch/mach-K302.h"
#elif defined(CONFIG_P2PF_E605)
#include "arch/mach-E605.h"
#elif defined(CONFIG_P2PF_K292)
#include "arch/mach-K292.h"
#elif defined(CONFIG_P2PF_K301)
#include "arch/mach-K301.h"
#elif defined(CONFIG_P2PF_K283)
#include "arch/mach-K283.h"
#elif defined(CONFIG_P2PF_K318)
#include "arch/mach-K318.h"
#elif defined(CONFIG_P2PF_K327)
#include "arch/mach-K327.h"
#elif defined(CONFIG_P2PF_K329)
#include "arch/mach-K329.h"
#elif defined(CONFIG_P2PF_SAV8313_K200)
#include "arch/mach-SAV8313_K200.h"
#elif defined(CONFIG_P2PF_SAV8313_K230)
#include "arch/mach-SAV8313_K230.h"
#elif defined(CONFIG_P2PF_MPC83XXBRB)
#include "arch/mach-MPC83XXBRB.h"
#elif defined(CONFIG_P2PF_MSU01)
#include "arch/mach-MSU01.h"
#elif defined(CONFIG_P2PF_BRB01)
#include "arch/mach-BRB01.h"
#else  /* CONFIG_P2PF_XXX */
#include "arch/mach-generic.h"
#endif /* CONFIG_P2PF_XXX */

#line __LINE__ "spd_hwif.c" /* Replace full path(__FILE__) to spd_hwif.c. */

#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,17)) /* KERNEL_VERSION : - 2.6.17 */
# define IRQF_SHARED SA_SHIRQ
#endif /* KERNEL_VERSION : - 2.6.17 */

static inline int  hwif_register_driver(void);
static inline void hwif_unregister_driver(void);
static int  hwif_attach(spd_dev_t *dev, struct pci_dev *pci);
static void hwif_detach(spd_dev_t *dev);

static void hwif_timer_handler(unsigned long arg);

#if (KERNEL_VERSION(2,6,19) <= LINUX_VERSION_CODE) /* KERNEL_VERSION : 2.6.19 - */
static irqreturn_t hwif_irq_handler(int irq, void *arg);
#else /* KERNEL_VERSION : - 2.6.18 */
static irqreturn_t hwif_irq_handler(int irq, void *arg, struct pt_regs *regs);
#endif /* KERNEL_VERSION : 2.6.19 - */

static inline int hwif_reset_device(spd_dev_t *dev);
static inline int hwif_check_ready(spd_dev_t *dev);
static int hwif_start_dma(spd_dev_t *dev);

static spd_hwif_t spd_hwif[SPD_N_DEV];
static const char errRate2retry[]={
  14, 7, 4, 3, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 0
};


static struct pci_device_id pci_ids[] = {
  {
    .vendor      = SPD_PCI_VENDOR_ID,
    .device      = SPD_PCI_DEVICE_ID,
    .subvendor   = PCI_ANY_ID,
    .subdevice   = PCI_ANY_ID,
    .driver_data = 0,
  },
  {
    .vendor      = SPD_PCI_VENDOR_ID,
    .device      = SPD_ADPT_PCI_DEVICE_ID,
    .subvendor   = PCI_ANY_ID,
    .subdevice   = PCI_ANY_ID,
    .driver_data = 0,
  },
  {0, }
};
MODULE_DEVICE_TABLE(pci, pci_ids);


static int hwif_probe(struct pci_dev *pci, const struct pci_device_id *pci_id)
{
  spd_dev_t *dev;
  int id;
  int retval;
  PTRACE();

  id = hwif_pci_to_id(pci);
  if(unlikely(id < 0 || id >= SPD_N_DEV)){
    PERROR("hwif_pci_to_id() failed(%d)", id);
    return -EINVAL;
  }
  dev = &spd_dev[id];

  retval = hwif_attach(dev, pci);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>hwif_attach() failed(%d)", dev->id+'a', retval);
    return retval;
  }

  return 0;
}


static void hwif_remove(struct pci_dev *pci)
{
  spd_dev_t *dev;
  int id;
  PTRACE();

  id = hwif_pci_to_id(pci);
  if(unlikely(id < 0 || id >= SPD_N_DEV)){
    PERROR("hwif_pci_to_id() failed(%d)", id);
    return;
  }
  dev = &spd_dev[id];

  hwif_detach(dev);
}


static struct pci_driver spd_driver = {
  .name     = "spd",
  .id_table = pci_ids,
  .probe    = hwif_probe,
  .remove   = hwif_remove,
};


static inline int hwif_register_driver(void)
{
  return pci_register_driver(&spd_driver);
}


static inline void hwif_unregister_driver(void)
{
  pci_unregister_driver(&spd_driver);
}


int spd_hwif_init(void)
{
  int i, retval;
  spd_dev_t *dev;
  PTRACE();

  memset(spd_hwif, 0, sizeof(spd_hwif));
  for(i = 0; i < SPD_N_DEV; i++){
    dev       = &spd_dev[i];
    dev->hwif = &spd_hwif[i];
    init_timer(&dev->hwif->timer);
    dev->hwif->timer.data     = (u32)dev;
    dev->hwif->timer.function = hwif_timer_handler;
    dev->hwif->request_irq    = 1;
  }
  retval = hwif_register_driver();
  if(unlikely(retval < 0)){
    PERROR("hwif_register_driver() failed(%d).", retval);
    return retval;
  }

  return 0;
}


int spd_hwif_exit(void)
{
  spd_dev_t *dev;
  int i;
  PTRACE();

  hwif_unregister_driver();

  for(i = 0; i < SPD_N_DEV; i++){
    dev = &spd_dev[i];
    del_timer_sync(&dev->hwif->timer);
  }

  return 0;
}


static inline int hwif_irq_disable(spd_dev_t *dev)
{
  u8 c;
  PTRACE();

  c = spd_in8(dev, SPD_REG_ASR);
  if(c&SPD_BIT_BSY){
    if(unlikely(!spd_is_lost(dev, 0)))
      PERROR("<spd%c>device busy", dev->id+'a');
  }
  c  = spd_in8(dev, SPD_REG_INT);
  c &= ~SPD_BIT_IRQE;
  spd_out8(dev, SPD_REG_INT, c);
  spd_out8(dev, SPD_REG_DCR, 0x02);

  return 0;
}


static inline int hwif_irq_enable(spd_dev_t *dev)
{
  u8 c;
  PTRACE();

  c = spd_in8(dev, SPD_REG_STR); /* reset irq */
  if(unlikely(c&SPD_BIT_BSY)){
    PERROR("<spd%c>device busy", dev->id+'a');
  }
  spd_out8(dev, SPD_REG_INT, SPD_BIT_IRQE);
  spd_out8(dev, SPD_REG_DCR, 0x00);

  return 0;
}


static inline int hwif_reset_device(spd_dev_t *dev)
{
  u8 c = 0, e = 0;
  u32 t = 30;
  unsigned long flags = 0;
  PTRACE();

  dev->time_stamp = jiffies;
  dev->ticks = 0;
  while(t--){
    unsigned long timeout = HZ/10;

    if(unlikely(spd_is_lost(dev, 1))){
      PERROR("<spd%c>device lost", dev->id+'a');
      return -ENODEV;
    }
    spin_lock_irqsave(&dev->lock, flags);
    e = spd_in8(dev, SPD_REG_ERR);
    c = spd_in8(dev, SPD_REG_ASR);

    if((c & SPD_BIT_BSY) == 0){
      spin_unlock_irqrestore(&dev->lock, flags);
      return 0;
    }
    spin_unlock_irqrestore(&dev->lock, flags);
    set_current_state(TASK_UNINTERRUPTIBLE);
    do{
      timeout = schedule_timeout(timeout);
    } while(timeout);
  }

  dev->ticks = (u32)((long)jiffies - (long)dev->time_stamp);
  PERROR("<spd%c>timeout error ASR=%02x ERR=%02x time=%dms", dev->id+'a', c, e, jiffies_to_msecs(dev->ticks));
  spd_send_event(dev, SPD_EVENT_CARD_ERROR, -EIO);

  return -EIO;
}


static inline int hwif_calc_checksum(u8 *buf)
{
  int sum = 0;
  int i;
  PTRACE();

  for(i = 0; i < SPD_HARDSECT_SIZE; i++){
    sum += *buf++;
  }
  PINFO(" sum=0x%X->0x%X", sum, sum&0xff);
  return (sum&0xff);
}


static inline int hwif_read_spec(spd_dev_t *dev)
{
  int retval;
  u16 *w;
  PTRACE();

  retval = spd_identify_device(dev, dev->tmp_buf);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>spd_identify_device() failed(%d)", dev->id+'a', retval);
    return retval;
  }

  w = (u16 *)dev->tmp_buf;
  dev->spec_version = w[135];
  dev->capacity     = ((w[61]<<16) + w[60]);
  dev->is_p2        = (w[144]>>8)?0:1;
  dev->is_up2       = w[144] & 0x0001;
  dev->is_lk        = w[145];
  dev->is_over      = w[146];

  PINFO("<spd%c>P2 spec version %d.%d",
        dev->id+'a', dev->spec_version>>4, dev->spec_version&0x0f);
  PINFO("<spd%c> capacity=%d is_p2=%d is_up2=%d is_lk=%d is_over=%d", dev->id+'a',
	dev->capacity, dev->is_p2, dev->is_up2, dev->is_lk, dev->is_over);

  return 0;
}


static inline int hwif_check_spec(spd_dev_t *dev)
{
  int major_version;
  PTRACE();

  major_version = (dev->spec_version>>4);

  if(unlikely(major_version > 4)){
    PERROR("<spd%c>card spec version(%02x) is wrong", dev->id+'a', dev->spec_version);
    return -EINVAL;
  }

  if(unlikely(dev->is_over && (4 == major_version))) {
    PERROR("<spd%c>card capacity is over", dev->id+'a');
    return -ENODEV;
  }

  return major_version;
}


static inline int hwif_read_csd(spd_dev_t *dev)
{
  int retval;
  u8 *b;
  int version;
  u8 error_rate;
  u32 dma_timeout, randw_rate;
  int dma_retry;
  PTRACE();

  b = (u8 *)dev->tmp_buf;

  version = hwif_check_spec(dev);
  if(unlikely(version < 0)){
    PERROR("<spd%c>hwif_check_spec() failed(%d)", dev->id+'a', version);
    return version;
  }

  if(version >= 3){
    retval = spd_log_sense(dev, 0x21, b);
    if(unlikely(retval < 0)){
      PERROR("<spd%c>spd_log_sense() failed(%d)", dev->id+'a', retval);
      return retval;
    }
  }

  if(version == 4){
    unsigned long tsc = (b[132]<<24|b[133]<<16|b[134]<<8|b[135]);
    unsigned long sys_area = 0;

    dev->n_area = 2;
    dev->area[0].start  = 0;
    dev->area[0].end    = sys_area - 1;
    dev->area[0].ausize = SPD_WSIZE_16K;
    dev->area[0].wsize  = SPD_WSIZE_16K;
    PINFO("<spd%c>System Area start=%08x end=%08x ru=%08x wsize=%08x",
	  dev->id+'a',
	  dev->area[0].start,
	  dev->area[0].end,
	  dev->area[0].ausize,
	  dev->area[0].wsize);

    dev->area[1].start  = sys_area;
    dev->area[1].end    = sys_area + tsc;
    dev->area[1].ausize = spd_sdsta2au(b[163]);
    dev->area[1].wsize  = hwif_get_usr_ru(dev);
    PINFO("<spd%c>  User Area start=%08x end=%08x au=%08x wsize=%08x",
	  dev->id+'a',
	  dev->area[1].start,
	  dev->area[1].end,
	  dev->area[1].ausize,
	  dev->area[1].wsize);

    dev->dma_timeout = dev->is_up2 ? ((3600*HZ)/1000) : (2*HZ);
    dev->dma_retry   = 1 + (dev->is_up2?1:0);
    PINFO("<spd%c>dma_timeout=%d(%dms), dma_retry=%d",
	  dev->id+'a', dev->dma_timeout, jiffies_to_msecs(dev->dma_timeout),
	  dev->dma_retry);
  } else if(version == 3){
    dev->n_area = 2;
    dev->area[0].start  = (b[37]<<24|b[38]<<16|b[39]<<8|b[40]);
    dev->area[0].end    = (b[41]<<24|b[42]<<16|b[43]<<8|b[44]);
    dev->area[0].ausize = (b[73]<<8|b[74]);
    dev->area[0].wsize  = hwif_get_sys_ru(dev);
    PINFO("<spd%c>System Area start=%08x end=%08x ru=%04x wsize=%04x",
	  dev->id+'a',
	  dev->area[0].start,
	  dev->area[0].end,
	  dev->area[0].ausize,
	  dev->area[0].wsize);

    dev->area[1].start  = (b[45]<<24|b[46]<<16|b[47]<<8|b[48]);
    dev->area[1].end    = (b[49]<<24|b[50]<<16|b[51]<<8|b[52]);
    dev->area[1].ausize = (b[77]<<8|b[78]);
    dev->area[1].wsize  = hwif_get_usr_ru(dev);
    PINFO("<spd%c>  User Area start=%08x end=%08x au=%04x wsize=%04x",
	  dev->id+'a',
	  dev->area[1].start,
	  dev->area[1].end,
	  dev->area[1].ausize,
	  dev->area[1].wsize);

    randw_rate = b[90]?b[90]:1;
    dma_timeout = ((b[124]<<8|b[125]) + (4*2000/randw_rate));
    error_rate = b[123];
    if(unlikely(error_rate == 0)){
      PERROR("<spd%c>invalid ErrorRate value(err_rate=%d)", dev->id+'a', error_rate);
      return -EINVAL;
    }
    if(error_rate > 15){
      dma_retry = 1;
    } else {
      dma_retry = errRate2retry[error_rate-1];
    }
    dev->dma_timeout = (dma_timeout*HZ)/1000;
    dev->dma_retry   = dma_retry;
    PINFO("<spd%c>dma_timeout=%d(%dms), dma_retry=%d, error_rate=%d",
          dev->id+'a', dev->dma_timeout, jiffies_to_msecs(dev->dma_timeout),
	  dma_retry, error_rate);
  } else {
    dev->n_area = 1;
    dev->area[0].start  = 0;
    dev->area[0].end    = dev->capacity - 1;
    dev->area[0].ausize = SPD_WSIZE_512K;
    dev->area[0].wsize  = SPD_WSIZE_512K;
    dev->dma_timeout    = SPD_DMA_TIMEOUT;
    dev->dma_retry      = SPD_N_RETRY;
    PINFO("<spd%c>       Area start=%08x end=%08x au=%04x wsize=%04x",
	  dev->id+'a',
	  dev->area[0].start,
	  dev->area[0].end,
	  dev->area[0].ausize,
	  dev->area[0].wsize);
    PINFO("<spd%c>dma_timeout=%d(%dms), dma_retry=%d",
          dev->id+'a', dev->dma_timeout, jiffies_to_msecs(dev->dma_timeout),
	  dev->dma_retry);
  }

  return 0;
}


static inline int hwif_check_irq(spd_dev_t *dev)
{
  u8 c;
  PTRACE();

  c = spd_in8(dev, SPD_REG_ISR);
  c &= SPD_BIT_IRQ;

  return (int)c;
}


static inline int hwif_check_lk(spd_dev_t *dev)
{
#if ! defined(SPD_FAST_CHECK_LK)
  u8 *b = dev->tmp_buf;
  int retval = 0;
#endif /* !SPD_FAST_CHECK_LK */
  PTRACE();

#if ! defined(SPD_FAST_CHECK_LK)
  retval = spd_get_linfo(dev, 0, b);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>spd_get_linfo() failed(%d)", dev->id+'a', retval);
    return retval;
  }

  DUMP_DATA(b, 16);
  PINFO("  linfo: 0x%02X\n", b[0]);
  if(b[0]&0x80){
    PINFO("  Locked");
    dev->is_lk = 1;
  } else {
    PINFO("  Unlocked");
    dev->is_lk = 0;
  }
#endif /* !SPD_FAST_CHECK_LK */

  return (dev->is_lk);
}


static int hwif_check_capacity(spd_dev_t *dev)
{
#if defined(SPD_WAIT_MINISD)
  u32 t = 0;
  int retval = 0;
  PTRACE();

  PERROR("<spd%c>Checking capacity...", dev->id+'a');
  t = 60; /* XXX */
  dev->time_stamp = jiffies;
  dev->ticks = 0;
  while(t--){
    unsigned long timeout = HZ; /* XXX */
    if(unlikely(spd_is_lost(dev, 1))){
      PERROR("<spd%c>device lost", dev->id+'a');
      return -ENODEV;
    }

    retval = hwif_read_spec(dev);
    if(unlikely(retval < 0)){
      PERROR("<spd%c>hwif_read_spec() failed(%d)", dev->id+'a', retval);
      return retval;
    }
    if(dev->capacity) {
      return 0;
    }

    set_current_state(TASK_UNINTERRUPTIBLE);
    do{
      timeout = schedule_timeout(timeout);
    } while(timeout);
  }

  dev->ticks = (u32)((long)jiffies - (long)dev->time_stamp);
  PERROR("<spd%c>timeout error time=%dms", dev->id+'a', jiffies_to_msecs(dev->ticks));
#endif /* SPD_WAIT_MINISD */
  spd_send_event(dev, SPD_EVENT_CARD_ERROR, -EIO);

  return -EIO;
}


static int hwif_set_features(spd_dev_t *dev, int major)
{
  int retval = 0;
  PTRACE();

  retval = spd_set_power_mode(dev, SPD_PM_LEVEL);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>spd_set_power_mode() failed(%d)", dev->id+'a', retval);
    return (retval);
  }
  if(major >= 3){
    /* Spec version 3.x - 4.x */
    retval = spd_set_interface_condition(dev, SPD_IO_UNLOCK);
    if(unlikely(retval < 0)){
      PERROR("<spd%c>spd_set_interface_condition() failed(%d)", dev->id+'a', retval);
      return (retval);
    }

    if(0x30 < dev->spec_version){
      /* Spec version 3.1 - 4.x */
      retval = spd_set_disable_cam_transfer(dev);
      if(unlikely(retval < 0)){
	PERROR("<spd%c>spd_set_disable_cam_transfer() failed(%d)", dev->id+'a', retval);
	return (retval);
      }
    }
  }
  HOTPLUG_OUT(14);
  retval = hwif_read_csd(dev);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>spd_read_csd() failed(%d)", dev->id+'a', retval);
    return (retval);
  }

  return (0);
}


static int hwif_attach(spd_dev_t *dev, struct pci_dev *pci)
{
  int retval = 0;
  int major = 0;
  unsigned long flags = 0;
  PTRACE();
  HOTPLUG_OUT(1);

  pci_dev_get(pci);
  spd_clr_status(dev, SPD_WRITE_GUARD);
  spd_set_status(dev, SPD_CARD_PRESENT);
  spd_send_event(dev, SPD_EVENT_CARD_INSERT, 0);

  spin_lock_irqsave(&dev->lock, flags);
  HOTPLUG_OUT(2);
  if(unlikely(spd_is_IOE(dev))){
    PALERT("<spd%c>duplicate probe", dev->id+'a');
    spin_unlock_irqrestore(&dev->lock, flags);
    pci_dev_put(pci);
    spd_send_event(dev, SPD_EVENT_FATAL_ERROR, 0);
    return -EINVAL;
  }
  HOTPLUG_OUT(3);
  retval = pci_enable_device(pci);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>pci_enable_device() failed(%d)", dev->id+'a', retval);
    spin_unlock_irqrestore(&dev->lock, flags);
    pci_dev_put(pci);
    spd_send_event(dev, SPD_EVENT_FATAL_ERROR, 1);
    return retval;
  }
  dev->dev                 = &pci->dev;
  HOTPLUG_OUT(4);
  dev->hwif->pci           = pci;
  dev->hwif->bus_no        = pci->bus->number;
  dev->hwif->devfn         = pci->devfn;
  dev->hwif->irq_number    = pci->irq;
  dev->hwif->base_address  = spd_ioremap(dev);
  dev->hwif->address_range = spd_resource_len(dev);
  if(unlikely(dev->hwif->base_address == 0)){
    PERROR("<spd%c>spd_ioremap() failed resource_start=0x%08x, len=0x%x",
           dev->id+'a',
	   dev->hwif->base_address,
	   dev->hwif->address_range);
    spin_unlock_irqrestore(&dev->lock, flags);
    pci_dev_put(pci);
    spd_send_event(dev, SPD_EVENT_FATAL_ERROR, 2);
    return -ENOMEM;
  }
  HOTPLUG_OUT(5);
  PINFO("<spd%c>BAR%d resource_start=0x%08x, resource_len=0x%x",
        dev->id+'a',
        SPD_IO_BAR,
        dev->hwif->base_address,
        dev->hwif->address_range);
  HOTPLUG_OUT(6);
  pci_set_master(pci);

  spin_unlock_irqrestore(&dev->lock, flags);
  retval = hwif_reset_device(dev);
  spin_lock_irqsave(&dev->lock, flags);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>hwif_reset_device() failed(%d)", dev->id+'a', retval);
    spin_unlock_irqrestore(&dev->lock, flags);
    goto ABORT;
  }
  HOTPLUG_OUT(7);
  hwif_irq_disable(dev);
  retval = request_irq(dev->hwif->irq_number, hwif_irq_handler,
		       IRQF_SHARED, "spd", (void *)dev);
  HOTPLUG_OUT(8);
  if(unlikely(retval != 0)){
    PERROR("<spd%c>request_irq() failed(%d)", dev->id+'a', retval);
    spin_unlock_irqrestore(&dev->lock, flags);
    spd_send_event(dev, SPD_EVENT_FATAL_ERROR, 3);
    retval = -EINVAL;
    goto ABORT;
  }
  HOTPLUG_OUT(9);
  dev->hwif->request_irq = retval;
  spin_unlock_irqrestore(&dev->lock, flags);  
  HOTPLUG_OUT(10);
  retval = hwif_read_spec(dev);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>hwif_read_spec() failed(%d)", dev->id+'a', retval);
    goto ABORT;
  }
  HOTPLUG_OUT(11);
  major = hwif_check_spec(dev);
  if(unlikely(major < 0)){
    PERROR("<spd%c>hwif_check_spec() failed(%d)", dev->id+'a', major);
    spd_set_status(dev, SPD_CARD_UNSUPPORT);
    spd_send_event(dev, SPD_EVENT_CARD_UNSUPPORT, major);
    goto ABORT;
  }
  HOTPLUG_OUT(12);
  if(major == 4){
    /* Spec version 4.x */
    if(dev->capacity == 0){
      if(dev->is_p2 || dev->is_up2){
	retval = hwif_check_lk(dev);
	if(unlikely(retval < 0)){
	  PERROR("<spd%c>hwif_check_lk() failed(%d)", dev->id+'a', retval);
	  goto ABORT;
	}

	if(retval == 1){
	  spd_set_LK(dev);
	  goto IOE;
	}
      } else {
	retval = hwif_check_capacity(dev);
	if(retval < 0){
	  goto ABORT;
	}
      }
    }
  }
  HOTPLUG_OUT(13);
  retval = hwif_set_features(dev, major);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>hwif_set_features() failed(%d)", dev->id+'a', retval);
    goto ABORT;
  }

 IOE:
  spd_set_IOE(dev);
  HOTPLUG_OUT(15);
  spd_send_event(dev, SPD_EVENT_CARD_READY, 0);
  HOTPLUG_OUT(16);
  return 0;

 ABORT:
  spin_lock_irqsave(&dev->lock, flags);
  hwif_irq_disable(dev);
  if(dev->hwif->request_irq == 0) {
    free_irq(dev->hwif->irq_number, (void *)dev);
    dev->hwif->request_irq = 1;
  }
  spd_iounmap(dev);
  dev->hwif->pci = NULL;
  spd_clr_IOE(dev);
  spin_unlock_irqrestore(&dev->lock, flags);
  pci_dev_put(pci);

  return retval;
}


static inline int hwif_get_io_dir(spd_dev_t *dev)
{
  switch(dev->hwif->cmd.command){
  case SPD_CMD_READ_SECTOR_DMA:
  case SPD_CMD_IDENTIFY_DEVICE:
  case SPD_CMD_PIO_DATAIN:
    return SPD_DIR_READ;

  case SPD_CMD_SEQ_WRITE_SECTOR_DMA:
  case SPD_CMD_WRITE_SECTOR_DMA:
  case SPD_CMD_PIO_DATAOUT:
    return SPD_DIR_WRITE;
  }
  return -EINVAL;
}


static void hwif_detach(spd_dev_t *dev)
{
  unsigned long flags = 0;
  struct pci_dev *pci = dev->hwif->pci;
  int ricoh = 0;
  int io_dir = 0;
  PTRACE();

  spin_lock_irqsave(&dev->lock, flags);
  hwif_irq_disable(dev);
  if(dev->hwif->request_irq == 0) {
    free_irq(dev->hwif->irq_number, (void *)dev);
    dev->hwif->request_irq = 1;
  }
  if(spd_is_IOE(dev)){
    spd_iounmap(dev);
  }
  dev->hwif->pci = NULL;
  dev->dev = NULL;
  pci_dev_put(pci);
  dev->retry = 0;
  spd_clr_IOE(dev);
  spd_clr_BUSY(dev);
  spd_clr_DEB(dev);
  spd_clr_LK(dev);
  spd_clr_status(dev, SPD_CARD_PRESENT);
  spd_clr_status(dev, SPD_CARD_UNSUPPORT);
  spd_clr_status(dev, SPD_WRITE_GUARD);

  if(spd_is_DMA(dev)){
    spd_clr_DMA(dev);
    ricoh = 1;
    io_dir = hwif_get_io_dir(dev);
  }
  spin_unlock_irqrestore(&dev->lock, flags);
  if(ricoh){
    spd_send_event(dev, SPD_EVENT_RICOH_ERROR, io_dir);
  }
  spd_send_event(dev, SPD_EVENT_CARD_REMOVE, 0);
}


static inline void hwif_cmd_set_sector(spd_command_t *cmd, u32 sector)
{
  cmd->drv_headnum = (u8)(0xe0 | ((sector>>24) & 0x0f));
  cmd->sec_num     = (u8)sector;
  cmd->cyl_low     = (u8)(sector >> 8);
  cmd->cyl_high    = (u8)(sector >> 16);
}


static inline void hwif_cmd_set_count(spd_command_t *cmd, u16 count)
{
  cmd->feature     = (u8)(count >> 8);
  cmd->sec_cnt     = (u8)count;
}


static inline u32 hwif_cmd_get_sector(spd_command_t *cmd)
{
  return ((cmd->drv_headnum&0x0f)<<24)|
          (cmd->cyl_high<<16)|
          (cmd->cyl_low<<8)|
          (cmd->sec_num);
}


static inline u16 hwif_cmd_get_count(spd_command_t *cmd)
{
  return (((u16)cmd->feature<<8)|cmd->sec_cnt);
}


#if (KERNEL_VERSION(2,6,19) <= LINUX_VERSION_CODE) /* KERNEL_VERSION : 2.6.19 - */
static irqreturn_t hwif_irq_handler(int irq, void *arg)
#else /* KERNEL_VERSION : - 2.6.18 */
static irqreturn_t hwif_irq_handler(int irq, void *arg, struct pt_regs *regs)
#endif /* KERNEL_VERSION : 2.6.19 - */
{
  spd_dev_t *dev = (spd_dev_t *)arg;
  u8 c;
  spd_command_t *cmd;
  PTRACE();

  HOTPLUG_OUT(17);
  if(!hwif_check_irq(dev)) return IRQ_NONE;
  HOTPLUG_OUT(60);
  if(!spd_is_DMA(dev)) {
    if(!spd_is_lost(dev, 0)){
      u8 c1, c2;
      c1 = spd_in8(dev, SPD_REG_ISR);
      c2 = spd_in8(dev, SPD_REG_STR); /* reset irq */
      PALERT("<spd%c>unknown interrupt. ISR=%02x, STR=%02x, base_address=%04x, reg_offset=%02x, irq=%d",
             dev->id+'a', c1, c2, dev->hwif->base_address,
             SPD_IO_REG_OFFSET, irq);
      udelay(1);
    }
    return IRQ_NONE;
  }

  HOTPLUG_OUT(61);
  if(spd_is_lost(dev, 0)) goto LOST;
  HOTPLUG_OUT(18);

  del_timer(&dev->hwif->timer);
  hwif_irq_disable(dev);

  DMA_LED_OFF();
  spd_clr_DMA(dev);
  spd_clr_BUSY(dev);

  HOTPLUG_OUT(19);
  c = spd_in8(dev, SPD_REG_STR); /* reset irq */
  dev->ticks = (u32)((long)jiffies - (long)dev->time_stamp);

  if(unlikely(c&SPD_BIT_BSY)){
    PERROR("<spd%c>device busy at irq handler!! ISR=%02x, STR=%02x", dev->id+'a', spd_in8(dev, SPD_REG_ISR), c);
  }
  if((c&SPD_BIT_ERR) == 0){
    if(dev->complete_handler != NULL){
      dev->complete_handler((void *)dev);
    }
    return IRQ_HANDLED;
  }
  HOTPLUG_OUT(20);
  cmd = &dev->hwif->cmd;
  switch(cmd->command){
  case SPD_CMD_READ_SECTOR_DMA:
    {
      PERROR("<spd%c>READ DMA error sector=%08x, count=%04x, STR=%02x, ERR=%02x, time=%dms",
             dev->id+'a',
             hwif_cmd_get_sector(cmd),
             hwif_cmd_get_count(cmd),
             c, spd_in8(dev, SPD_REG_ERR), jiffies_to_msecs(dev->ticks));
      break;
    }
      
  case SPD_CMD_WRITE_SECTOR_DMA:
    {
      PERROR("<spd%c>WRITE DMA error sector=%08x, count=%04x, STR=%02x, ERR=%02x, time=%dms",
             dev->id+'a',
             hwif_cmd_get_sector(cmd),
             hwif_cmd_get_count(cmd),
             c, spd_in8(dev, SPD_REG_ERR), jiffies_to_msecs(dev->ticks));
      break;
    }

  case SPD_CMD_SEQ_WRITE_SECTOR_DMA:
    {
      PERROR("<spd%c>SEQ WRITE DMA error sector=%08x, count=%04x, STR=%02x, ERR=%02x, time=%dms",
             dev->id+'a',
             hwif_cmd_get_sector(cmd),
             hwif_cmd_get_count(cmd),
             c, spd_in8(dev, SPD_REG_ERR), jiffies_to_msecs(dev->ticks));
    }
  }

  dev->errcode = -EIO;

  if(dev->retry){
    PERROR("<spd%c>retry count=%d", dev->id+'a', dev->retry);
    dev->retry--;
    dev->errcode = hwif_start_dma(dev);
    if(dev->errcode == 0){
      return IRQ_HANDLED;
    }
  }

  spd_set_status(dev, SPD_WRITE_GUARD);
  spd_send_event(dev, SPD_EVENT_CARD_ERROR, dev->errcode);  

  if(dev->complete_handler != NULL){
    dev->complete_handler((void *)dev);
  }

  return IRQ_HANDLED;

 LOST:
  dev->errcode = -ENODEV;
  del_timer(&dev->hwif->timer);
  if(dev->complete_handler != NULL){
    dev->complete_handler((void *)dev);
  }

  return IRQ_HANDLED;
}


static void hwif_timer_handler(unsigned long arg)
{
  unsigned long flags = 0;
  spd_dev_t *dev = (spd_dev_t *)arg;
  spd_command_t *cmd;
  PTRACE();

  spin_lock_irqsave(&dev->lock, flags);
  del_timer(&dev->hwif->timer);
  dev->ticks = (u32)((long)jiffies - (long)dev->time_stamp);

  DMA_LED_OFF();
  spd_clr_DMA(dev);
  spd_clr_BUSY(dev);
  spin_unlock_irqrestore(&dev->lock, flags);

  if(spd_is_lost(dev, 1)){
    PERROR("<spd%c>device lost", dev->id+'a');
    dev->errcode = -ENODEV;

    if(dev->complete_handler != NULL){
      dev->complete_handler((void *)dev);
    }
    return;
  }

  spin_lock_irqsave(&dev->lock, flags);
  hwif_irq_disable(dev);
  cmd = &dev->hwif->cmd;
  switch(cmd->command){
  case SPD_CMD_READ_SECTOR_DMA:
    {
      PERROR("<spd%c>READ DMA timedout sector=%08x, count=%04x, STR=%02x, ERR=%02x, time=%dms",
             dev->id+'a',
             hwif_cmd_get_sector(cmd),
             hwif_cmd_get_count(cmd),
             spd_in8(dev, SPD_REG_STR),
             spd_in8(dev, SPD_REG_ERR), jiffies_to_msecs(dev->ticks));
      break;
    }
      
  case SPD_CMD_WRITE_SECTOR_DMA:
    {
      PERROR("<spd%c>WRITE DMA timedout sector=%08x, count=%04x, STR=%02x, ERR=%02x, time=%dms",
	     dev->id+'a',
             hwif_cmd_get_sector(cmd),
             hwif_cmd_get_count(cmd),
             spd_in8(dev, SPD_REG_STR),
             spd_in8(dev, SPD_REG_ERR), jiffies_to_msecs(dev->ticks));
      break;
    }

  case SPD_CMD_SEQ_WRITE_SECTOR_DMA:
    {
      PERROR("<spd%c>SEQ WRITE DMA timedout sector=%08x, count=%04x, STR=%02x, ERR=%02x, time=%dms",
             dev->id+'a',
             hwif_cmd_get_sector(cmd),
             hwif_cmd_get_count(cmd),
             spd_in8(dev, SPD_REG_STR),
             spd_in8(dev, SPD_REG_ERR), jiffies_to_msecs(dev->ticks));
    }
  }
  dev->errcode = -ETIMEDOUT;
  spd_set_status(dev, SPD_WRITE_GUARD);
  spd_send_event(dev,SPD_EVENT_CARD_ERROR, dev->errcode);

  if(dev->complete_handler != NULL){
    dev->complete_handler((void *)dev);
  }

  spin_unlock_irqrestore(&dev->lock, flags);
  return;
}


static inline int hwif_read_data32(spd_dev_t *dev, u32 *buf, u16 count)
{
  int i;

  for(i=0; i<count; i++){
    *buf++ = spd_in32(dev, SPD_REG_DATA);
  }

  return 0;
}


static inline int hwif_read_data16(spd_dev_t *dev, u16 *buf, u16 count)
{
  int i;
  u32 tmp;
  int skip = sizeof(u32)/sizeof(*buf);

  for(i=0; i<count/skip; i++){
    tmp        = spd_in32(dev, SPD_REG_DATA);
    buf[i*skip  ] = (u16)( tmp      & 0xFFFF);
    buf[i*skip+1] = (u16)((tmp>>16) & 0xFFFF);
  }

  return 0;
}


static inline int hwif_read_data8(spd_dev_t *dev, u8 *buf, u16 count)
{
  int i;
  u32 tmp;
  int skip = sizeof(u32)/sizeof(*buf);

  for(i=0; i<count/skip; i++){
    tmp        = spd_in32(dev, SPD_REG_DATA);
    buf[i*skip  ] = (u8)( tmp      & 0xFF);
    buf[i*skip+1] = (u8)((tmp>> 8) & 0xFF);
    buf[i*skip+2] = (u8)((tmp>>16) & 0xFF);
    buf[i*skip+3] = (u8)((tmp>>24) & 0xFF);
  }

  return 0;
}


static inline int hwif_write_data32(spd_dev_t *dev, u32 *buf, u16 count)
{
  int i;

  for(i=0; i<count; i++){
    spd_out32(dev, SPD_REG_DATA, *buf++);
  }

  return 0;
}


static inline int hwif_write_data16(spd_dev_t *dev, u16 *buf, u16 count)
{
  int i;
  u32 tmp;
  int skip = sizeof(u32)/sizeof(*buf);

  for(i=0; i<count/skip; i++){
    tmp = 0;
    tmp = (u32)(( (u32)(buf[i*skip  ])     & 0x0000FFFF)
		|((u32)(buf[i*skip+1]<<16) & 0xFFFF0000));

    spd_out32(dev, SPD_REG_DATA, tmp);
  }
  
  return 0;
}


static inline int hwif_write_data8(spd_dev_t *dev, u8 *buf, u16 count)
{
  int i;
  u32 tmp;
  int skip = sizeof(u32)/sizeof(*buf);

  for(i=0; i<count/skip; i++){
    tmp = 0;
    tmp = (u32)(( (u32)(buf[i*skip  ])     & 0x000000FF)
		|((u32)(buf[i*skip+1]<< 8) & 0x0000FF00)
		|((u32)(buf[i*skip+2]<<16) & 0x00FF0000)
		|((u32)(buf[i*skip+3]<<24) & 0XFF000000));

    spd_out32(dev, SPD_REG_DATA, tmp);
  }

  return 0;
}


static inline int hwif_check_ready(spd_dev_t *dev)
{
  u32 t;
  u8 c = 0;
  PTRACE()

  t = 10;
  dev->time_stamp = jiffies;
  dev->ticks = 0;
  while(t--){
    if(unlikely(spd_is_lost(dev, 1))){
      PERROR("<spd%c>device lost", dev->id+'a');
      return -ENODEV;
    }
    HOTPLUG_OUT(21);
    c = spd_in8(dev, SPD_REG_ASR);
    if((c & SPD_BIT_BSY) == 0){
      return 0;
    }
    udelay(1);
  }

  dev->ticks = (u32)((long)jiffies - (long)dev->time_stamp);
  PERROR("<spd%c>timeout error ASR=%02x ERR=%02x time=%dms",
	 dev->id+'a', c, spd_in8(dev, SPD_REG_ERR), jiffies_to_msecs(dev->ticks));

  return -ETIMEDOUT;
}


static inline int hwif_wait_transfer(spd_dev_t *dev)
{
  u32 t;
  u8 c = 0;
  PTRACE();

  t = dev->timeout*1000*1000/HZ/2;
  dev->time_stamp = jiffies;
  dev->ticks = 0;
  while(t--){
    if(unlikely(spd_is_lost(dev, 1))){
      PERROR("<spd%c>device lost", dev->id+'a');
      return -ENODEV;
    }
    HOTPLUG_OUT(22);
    c = spd_in8(dev, SPD_REG_STR);
    if(c&SPD_BIT_BSY){
      udelay(1);
      continue;
    }
    if(unlikely(c&SPD_BIT_ERR)){
      PERROR("<spd%c>I/O error STR=%02x ERR=%02x",
             dev->id+'a', c, spd_in8(dev, SPD_REG_ERR));
      spd_send_event(dev, SPD_EVENT_CARD_ERROR, -EIO);
      return -EIO;
    }
    if(c&SPD_BIT_DRQ){
      return 0;
    }
    udelay(1);
  }

  dev->ticks = (u32)((long)jiffies - (long)dev->time_stamp);
  PERROR("<spd%c>timeout error STR=%02x ERR=%02x time=%dms", 
         dev->id+'a',
	 spd_in8(dev, SPD_REG_STR),
         spd_in8(dev, SPD_REG_ERR),
	 jiffies_to_msecs(dev->ticks));

  return -ETIMEDOUT;
}			 


static inline int hwif_pio_nodata_command(spd_dev_t *dev)
{
  u32 t;
  u8  c = 0;
  int retval = 0;
  unsigned long flags = 0;
  spd_command_t *cmd = &dev->hwif->cmd;
  PTRACE();

  spin_lock_irqsave(&dev->lock, flags);
  if(spd_is_BUSY(dev)){
    PERROR("<spd%c>device busy", dev->id+'a');
    spin_unlock_irqrestore(&dev->lock, flags);
    return -EBUSY;
  }
  spd_set_BUSY(dev);
  hwif_irq_disable(dev);
  spin_unlock_irqrestore(&dev->lock, flags);

 RETRY:
  retval = hwif_check_ready(dev);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>hwif_check_ready() failed(%d)", dev->id+'a', retval);
    goto ABORT;
  }

  spd_out8(dev, SPD_REG_DHR, cmd->drv_headnum);
  spd_out8(dev, SPD_REG_FTR, cmd->feature);
  spd_out8(dev, SPD_REG_SCR, cmd->sec_cnt);
  spd_out8(dev, SPD_REG_SNR, cmd->sec_num);
  spd_out8(dev, SPD_REG_CLR, cmd->cyl_low);
  spd_out8(dev, SPD_REG_CHR, cmd->cyl_high);
  spd_out8(dev, SPD_REG_CMD, cmd->command);
  udelay(1);

  t = dev->timeout*1000*1000/HZ/2;
  dev->time_stamp = jiffies;
  dev->ticks = 0;
  while(t--){
    if(unlikely(spd_is_lost(dev, 1))){
      PERROR("<spd%c>device lost", dev->id+'a');
      retval = -ENODEV;
      goto ABORT;
    }
    c = spd_in8(dev, SPD_REG_STR);
    if(c&SPD_BIT_BSY){
      udelay(1);
      continue;
    }
    if(unlikely(c&SPD_BIT_ERR)){
      PERROR("<spd%c>I/O error CMD=%02x STR=%02x ERR=%02x",
             dev->id+'a', cmd->command, c, spd_in8(dev, SPD_REG_ERR));
      retval = -EIO;
      goto ABORT;
    }
    spd_clr_BUSY(dev);
    return 0;
  }

  dev->ticks = (u32)((long)jiffies - (long)dev->time_stamp);
  PERROR("<spd%c>timeout error CMD=%02x STR=%02x ERR=%02x time=%dms",
         dev->id+'a', cmd->command, c, spd_in8(dev, SPD_REG_ERR),
	 jiffies_to_msecs(dev->ticks));
  retval = -ETIMEDOUT;

 ABORT:
  if(retval == -ENODEV){
    spd_clr_BUSY(dev);
    return retval;
  }
  if(dev->retry){
    PERROR("<spd%c>retry count=%d", dev->id+'a', dev->retry);
    dev->retry--;
    goto RETRY;
  }
  spd_clr_BUSY(dev);
  spd_send_event(dev, SPD_EVENT_CARD_ERROR, retval);  

  return retval;
}


static inline int hwif_pio_dataout_command(spd_dev_t *dev, void *buf,
					   int count, int type)
{
  u32 t;
  int i;
  int retval = 0;
  unsigned long flags = 0;
  u8 c = 0;
  spd_command_t *cmd = &dev->hwif->cmd;
  PTRACE();

  spin_lock_irqsave(&dev->lock, flags);
  if(spd_is_BUSY(dev)){
    PERROR("<spd%c>device busy", dev->id+'a');
    spin_unlock_irqrestore(&dev->lock, flags);
    return -EBUSY;
  }
  spd_set_BUSY(dev);
  hwif_irq_disable(dev);
  spin_unlock_irqrestore(&dev->lock, flags);
  
 RETRY:
  retval = hwif_check_ready(dev);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>hwif_check_ready() failed(%d)", dev->id+'a', retval);
    spd_clr_BUSY(dev);
    return retval;
  }

  spd_out8(dev, SPD_REG_DHR, cmd->drv_headnum);
  spd_out8(dev, SPD_REG_CT3, 0x00);
  spd_out8(dev, SPD_REG_FTR, cmd->feature);
  spd_out8(dev, SPD_REG_SCR, cmd->sec_cnt);
  spd_out8(dev, SPD_REG_SNR, cmd->sec_num);
  spd_out8(dev, SPD_REG_CLR, cmd->cyl_low);
  spd_out8(dev, SPD_REG_CHR, cmd->cyl_high);
  spd_out8(dev, SPD_REG_CMD, cmd->command);
  udelay(1);

  for(i = 0; i < count; i++){
    retval = hwif_wait_transfer(dev);
    if(unlikely(retval < 0)){
      PERROR("<spd%c>hwif_wait_transfer() failed(%d)", dev->id+'a', retval);
      goto ABORT;
    }

    switch(type){
    case 32:
      {
	hwif_write_data32(dev, (u32 *)buf, SPD_HARDSECT_SIZE/sizeof(u32));
	buf += (SPD_HARDSECT_SIZE/sizeof(u32));
	break;
      }
	
    case 16:
      {
	hwif_write_data16(dev, (u16 *)buf, SPD_HARDSECT_SIZE/sizeof(u16));
	buf += (SPD_HARDSECT_SIZE/sizeof(u16));
	break;
      }

    case 8:
      {
	hwif_write_data8(dev, (u8 *)buf, SPD_HARDSECT_SIZE/sizeof(u8));
	buf += (SPD_HARDSECT_SIZE/sizeof(u8));
	break;
      }

    default:
      {
	PERROR("<spd%c>invalid variable type(%d)", dev->id+'a', type);
	retval = -EINVAL;
	goto ABORT;
      }
    }
  }

  t = dev->timeout*1000*1000/HZ/2;
  dev->time_stamp = jiffies;
  dev->ticks = 0;
  while(t--){
    if(unlikely(spd_is_lost(dev, 1))){
      PERROR("<spd%c>device lost", dev->id+'a');
      retval = -ENODEV;
      goto ABORT;
    }
    c = spd_in8(dev, SPD_REG_ASR);
    if(c&SPD_BIT_BSY){
      udelay(1);
      continue;
    }
    if(unlikely(c&SPD_BIT_ERR)){
      PERROR("<spd%c>I/O error CMD=%02x ASR=%02x ERR=%02x",
             dev->id+'a', cmd->command, c, spd_in8(dev, SPD_REG_ERR));
      retval = -EIO;
      goto ABORT;
    }
    spd_clr_BUSY(dev);
    return 0;
  }

  dev->ticks = (u32)((long)jiffies - (long)dev->time_stamp);
  PERROR("<spd%c>timeout error CMD=%02x ASR=%02x ERR=%02x time=%dms",
         dev->id+'a', cmd->command, c, spd_in8(dev, SPD_REG_ERR),
	 jiffies_to_msecs(dev->ticks));
  retval = -ETIMEDOUT;

 ABORT:
  if(retval == -ENODEV){
    spd_clr_BUSY(dev);
    return retval;
  }
  if(dev->retry){
    PERROR("<spd%c>retry count=%d", dev->id+'a', dev->retry);
    dev->retry--;
    goto RETRY;
  }
  spd_clr_BUSY(dev);
  spd_send_event(dev, SPD_EVENT_CARD_ERROR, retval);  

  return retval;
}


static inline int hwif_pio_datain_command(spd_dev_t *dev, void *buf,
					  int count, int type)
{
  u32 t;
  int i;
  int retval = 0;
  unsigned long flags = 0;
  u8 c = 0;
  spd_command_t *cmd = &dev->hwif->cmd;
  PTRACE();

  spin_lock_irqsave(&dev->lock, flags);
  if(spd_is_BUSY(dev)){
    PERROR("<spd%c>device busy", dev->id+'a');
    spin_unlock_irqrestore(&dev->lock, flags);
    return -EBUSY;
  }
  spd_set_BUSY(dev);
  hwif_irq_disable(dev);
  spin_unlock_irqrestore(&dev->lock, flags);

RETRY:
  retval = hwif_check_ready(dev);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>hwif_check_ready() failed(%d)", dev->id+'a', retval);
    goto ABORT;
  }

  spd_out8(dev, SPD_REG_DHR, cmd->drv_headnum);
  spd_out8(dev, SPD_REG_CT3, 0x02);
  spd_out8(dev, SPD_REG_FTR, cmd->feature);
  spd_out8(dev, SPD_REG_SCR, cmd->sec_cnt);
  spd_out8(dev, SPD_REG_SNR, cmd->sec_num);
  spd_out8(dev, SPD_REG_CLR, cmd->cyl_low);
  spd_out8(dev, SPD_REG_CHR, cmd->cyl_high);
  spd_out8(dev, SPD_REG_CMD, cmd->command);
  udelay(1);

  for(i = 0; i < count; i++){
    retval = hwif_wait_transfer(dev);
    if(unlikely(retval < 0)){
      PERROR("<spd%c>hwif_wait_transfer() failed(%d)", dev->id+'a', retval);
      goto ABORT;
    }

    switch(type){
    case 32:
      {
	hwif_read_data32(dev, (u32 *)buf, SPD_HARDSECT_SIZE/sizeof(u32));
	buf += (SPD_HARDSECT_SIZE/sizeof(u32));
	break;
      }

    case 16:
      {
	hwif_read_data16(dev, (u16 *)buf, SPD_HARDSECT_SIZE/sizeof(u16));
	buf += (SPD_HARDSECT_SIZE/sizeof(u16));
	break;
      }

    case 8:
      {
	hwif_read_data8(dev, (u8 *)buf, SPD_HARDSECT_SIZE/sizeof(u8));
	buf += (SPD_HARDSECT_SIZE/sizeof(u8));
	break;
      }

    default:
      {
	PERROR("<spd%c>invalid variable type(%d)", dev->id+'a', type);
	retval = -EINVAL;
	goto ABORT;
      }
    }
  }

  t = dev->timeout*1000*1000/HZ/2;
  dev->time_stamp = jiffies;
  dev->ticks = 0;
  while(t--){
    if(unlikely(spd_is_lost(dev, 1))){
      PERROR("<spd%c>device lost", dev->id+'a');
      retval = -ENODEV;
      goto ABORT;
    }
    c = spd_in8(dev, SPD_REG_ASR);
    if(c&SPD_BIT_BSY){
      udelay(1);
      continue;
    }
    if(unlikely(c&SPD_BIT_ERR)){
      PERROR("<spd%c>I/O error CMD=%02x ASR=%02x ERR=%02x",
             dev->id+'a', cmd->command, c, spd_in8(dev, SPD_REG_ERR));
      retval = -EIO;
      goto ABORT;
    }
    spd_clr_BUSY(dev);
    return 0;
  }

  dev->ticks = (u32)((long)jiffies - (long)dev->time_stamp);
  PERROR("<spd%c>timeout error CMD=%02x ASR=%02x ERR=%02x time=%dms",
         dev->id+'a', cmd->command, c, spd_in8(dev, SPD_REG_ERR),
	 jiffies_to_msecs(dev->ticks));
  retval = -ETIMEDOUT;

 ABORT:
  if(retval == -ENODEV){
    spd_clr_BUSY(dev);
    return retval;
  }
  if(dev->retry){
    PERROR("<spd%c>retry count=%d", dev->id+'a', dev->retry);
    dev->retry--;
    goto RETRY;
  }
  spd_clr_BUSY(dev);
  spd_send_event(dev, SPD_EVENT_CARD_ERROR, retval);  

  return retval;
}


static inline int hwif_start_timer(spd_dev_t *dev)
{
  if(dev->timeout == (u32)-1){
    return 0;
  }
  if(dev->hwif->timer.function != NULL){
    dev->hwif->timer.expires = dev->timeout + jiffies;
    add_timer(&dev->hwif->timer);
  }
  return 0;
}


static int hwif_start_dma(spd_dev_t *dev)
{
  int retval;
  unsigned long flags = 0;
  spd_command_t *cmd = &dev->hwif->cmd;
  PTRACE();
  
  spin_lock_irqsave(&dev->lock, flags);

#if defined(DBG_HOTPLUG_TEST)  
  if(SPD_CMD_READ_SECTOR_DMA == cmd->command)
    HOTPLUG_OUT(62);
  else
    HOTPLUG_OUT(63);
#endif /* DBG_HOTPLUG_TEST */

  if(spd_is_BUSY(dev)){
    PERROR("<spd%c>device busy", dev->id+'a');
    spin_unlock_irqrestore(&dev->lock, flags);
    return -EBUSY;
  }
  spd_set_BUSY(dev);
  hwif_irq_disable(dev);
  spin_unlock_irqrestore(&dev->lock, flags);

 RETRY:
  retval = hwif_check_ready(dev);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>hwif_check_ready() failed(%d)", dev->id+'a', retval);
    goto ABORT;
  }
  spd_in8(dev, SPD_REG_STR); /* reset irq */
  spd_out8(dev, SPD_REG_DHR, cmd->drv_headnum);
  spd_out8(dev, SPD_REG_DCR, 0x00);
  spd_out8(dev, SPD_REG_CT3, 0x01);
  spd_out8(dev, SPD_REG_FTR, cmd->feature);
  spd_out8(dev, SPD_REG_SCR, cmd->sec_cnt);
  spd_out8(dev, SPD_REG_SNR, cmd->sec_num);
  spd_out8(dev, SPD_REG_CLR, cmd->cyl_low);
  spd_out8(dev, SPD_REG_CHR, cmd->cyl_high);

  hwif_irq_enable(dev);  
  hwif_start_timer(dev);
  spd_set_DMA(dev);
  spd_out8(dev, SPD_REG_CMD, cmd->command);
  DMA_LED_ON();
  HOTPLUG_OUT(23);
  return 0;

 ABORT:
  if(retval == -ENODEV){
    spd_clr_DMA(dev);
    spd_clr_BUSY(dev);
    return retval;
  }
  if(dev->retry){
    PERROR("<spd%c>retry count=%d", dev->id+'a', dev->retry);
    dev->retry--;
    goto RETRY;
  }
  spd_clr_DMA(dev);
  spd_clr_BUSY(dev);
  spd_send_event(dev, SPD_EVENT_CARD_ERROR, retval);  

  return retval;
}


int spd_read_sector(spd_dev_t *dev,
		    u32 sector, u16 count,
		    spd_scatterlist_t *sg)
{
  int retval;
  spd_command_t *cmd = &dev->hwif->cmd;
  PTRACE();

  PRINT_REQUEST(dev, SPD_DIR_READ, sector, count);
  PINFO("<spd%c>READ DMA:sector=%08x, count=%04x, addr=%08x",
	dev->id+'a', sector, count, le32_to_cpu(sg->bus_address));
  PCOMMAND("<spd%c>READ DMA:sector=%08x, count=%04x, addr=%08x",
	   dev->id+'a', sector, count, le32_to_cpu(sg->bus_address));

  dev->time_stamp = jiffies;
  dev->ticks	  = 0;
  dev->errcode	  = 0;

  hwif_cmd_set_sector(cmd, sector);
  hwif_cmd_set_count(cmd, count);
  cmd->command     = SPD_CMD_READ_SECTOR_DMA;

  spd_out32(dev, SPD_REG_HOST_ADDRESS, (u32)virt_to_bus((void *)sg));
  hwif_adjust_pci(dev, SPD_DIR_READ, hwif_target_of(le32_to_cpu(sg->bus_address)));
  hwif_adjust_latency_timer(dev);

  retval = hwif_start_dma(dev);
  dev->errcode = retval;
  dev->ticks   = (u32)((long)jiffies - (long)dev->time_stamp);
  if(unlikely(retval < 0)){
    spd_set_status(dev, SPD_WRITE_GUARD);
    PERROR("<spd%c>hwif_start_dma() failed(%d)", dev->id+'a', retval);
  }
  return retval;
}


int spd_write_sector(spd_dev_t *dev,
		     u32 sector, u16 count,
		     spd_scatterlist_t *sg)
{
  int retval;
  spd_command_t *cmd = &dev->hwif->cmd;
  PTRACE();

  PRINT_REQUEST(dev, SPD_DIR_WRITE, sector, count);
  PINFO("<spd%c>WRITE DMA:sector=%08x, count=%04x, addr=%08x",
	dev->id+'a', sector, count, le32_to_cpu(sg->bus_address));
  PCOMMAND("<spd%c>WRITE DMA:sector=%08x, count=%04x, addr=%08x",
           dev->id+'a', sector, count, le32_to_cpu(sg->bus_address));

  dev->time_stamp = jiffies;
  dev->ticks	  = 0;
  dev->errcode	  = 0;

  hwif_cmd_set_sector(cmd, sector);
  hwif_cmd_set_count(cmd, count);
  cmd->command     = SPD_CMD_WRITE_SECTOR_DMA;

  spd_out32(dev, SPD_REG_HOST_ADDRESS, (u32)virt_to_bus((void *)sg));
  hwif_adjust_pci(dev, SPD_DIR_WRITE, hwif_target_of(le32_to_cpu(sg->bus_address)));
  hwif_adjust_latency_timer(dev);
  
  retval = hwif_start_dma(dev);
  dev->errcode = retval;
  dev->ticks   = (u32)((long)jiffies - (long)dev->time_stamp);
  if(unlikely(retval < 0)){
    spd_set_status(dev, SPD_WRITE_GUARD);
    PERROR("<spd%c>hwif_start_dma() failed(%d)", dev->id+'a', retval);
  }
  return retval;
}


static inline int hwif_sector_is_user(spd_dev_t *dev, u32 sector)
{
  if((dev->spec_version&0xf0) < 0x30){
    return 0;
  }
  if(dev->area[1].start > sector || dev->area[1].end < sector){
    return 0;
  }
  return 1;
}


int spd_seq_write_sector(spd_dev_t *dev,
			 u32 sector, u16 count,
			 spd_scatterlist_t *sg)
{
  int retval;
  spd_command_t *cmd = &dev->hwif->cmd;
  PTRACE();

  if(!hwif_sector_is_user(dev, sector)){
    return spd_write_sector(dev, sector, count, sg);
  }

  PRINT_REQUEST(dev, SPD_DIR_WRITE, sector, count);
  PINFO("<spd%c>SEQ WRITE DMA:sector=%08x, count=%04x, addr=%08x",
	dev->id+'a', sector, count, le32_to_cpu(sg->bus_address));
  PCOMMAND("<spd%c>SEQ WRITE DMA:sector=%08x, count=%04x, addr=%08x",
           dev->id+'a', sector, count, le32_to_cpu(sg->bus_address));

  dev->time_stamp = jiffies;
  dev->ticks	  = 0;
  dev->errcode	  = 0;

  hwif_cmd_set_sector(cmd, sector);
  hwif_cmd_set_count(cmd, count);
  cmd->command     = SPD_CMD_SEQ_WRITE_SECTOR_DMA;

  spd_out32(dev, SPD_REG_HOST_ADDRESS, (u32)virt_to_bus((void *)sg));
  hwif_adjust_pci(dev, SPD_DIR_WRITE, hwif_target_of(le32_to_cpu(sg->bus_address)));
  hwif_adjust_latency_timer(dev);

  retval = hwif_start_dma(dev);
  dev->errcode = retval;
  dev->ticks   = (u32)((long)jiffies - (long)dev->time_stamp);
  if(unlikely(retval < 0)){
    spd_set_status(dev, SPD_WRITE_GUARD);
    PERROR("<spd%c>hwif_start_dma() failed(%d)", dev->id+'a', retval);
  }
  return retval;
}


int spd_identify_device(spd_dev_t *dev, u16 *buf)
{
  int retval;
  spd_command_t *cmd = &dev->hwif->cmd;
  PTRACE();
  PCOMMAND("<spd%c>IDENTIFY DEVICE", dev->id+'a');

  cmd->feature     = 0;
  cmd->sec_cnt     = 0;
  cmd->sec_num     = 0;
  cmd->cyl_low     = 0;
  cmd->cyl_high    = 0;
  cmd->drv_headnum = 0;
  cmd->command     = SPD_CMD_IDENTIFY_DEVICE;

  retval = hwif_pio_datain_command(dev, (void *)buf, 1, 16);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>hwif_pio_datain_command() failed(%d)", dev->id+'a', retval);
    return retval;
  }

  retval = hwif_calc_checksum((u8 *)buf);
  if(unlikely(retval != 0)){
    PERROR("<spd%c>hwif_calc_checksum() error(sum=%d)", dev->id+'a', retval);
    spd_send_event(dev, SPD_EVENT_CARD_ERROR, -EIO);
    return -EIO;
  }

  return 0;
}


int spd_sector_erase(spd_dev_t *dev, u32 sector, u8 count)
{
  int retval;
  spd_command_t *cmd = &dev->hwif->cmd;
  PTRACE();
  PCOMMAND("<spd%c>SECTOR ERASE sector=%08x, count=%04x", dev->id+'a', sector, count);

  cmd->feature     = SPD_CMD_SECTOR_ERASE;
  cmd->sec_cnt     = count;
  cmd->sec_num     = (sector&0xff);
  cmd->cyl_low     = ((sector>>8 )&0xff);
  cmd->cyl_high    = ((sector>>16)&0xff);
  cmd->drv_headnum = (((sector>>24)&0x0f)|0x40);
  cmd->command     = SPD_CMD_PIO_NODATA;

  retval = hwif_pio_nodata_command(dev);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>hwif_pio_nodata_command() failed(%d)", dev->id+'a', retval);
  }
  return retval;
}


int spd_block_erase(spd_dev_t *dev, u32 sector, u8 count)
{
  int retval;
  spd_command_t *cmd = &dev->hwif->cmd;
  PTRACE();
  PCOMMAND("<spd%c>BLOCK ERASE sector=%08x, count=%04x", dev->id+'a', sector, count);

  cmd->feature     = SPD_CMD_BLOCK_ERASE;
  cmd->sec_cnt     = count;
  cmd->sec_num     = (sector&0xff);
  cmd->cyl_low     = ((sector>>8 )&0xff);
  cmd->cyl_high    = ((sector>>16)&0xff);
  cmd->drv_headnum = (((sector>>24)&0x0f)|0x40);
  cmd->command     = SPD_CMD_PIO_NODATA;

  retval = hwif_pio_nodata_command(dev);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>hwif_pio_nodata_command() failed(%d)", dev->id+'a', retval);
  }
  return retval;
}


int spd_au_erase(spd_dev_t *dev, u32 sector, u8 count)
{
  int retval;
  spd_command_t *cmd = &dev->hwif->cmd;
  PTRACE();
  PCOMMAND("<spd%c>AU ERASE sector=%08x, AU count=%04x", dev->id+'a', sector, count);

  if(unlikely(count != 1)){
    PALERT("<spd%c>Unsupported AU Erase of over 1AU(%d)!", dev->id+'a', count);
  }

  cmd->feature     = SPD_CMD_AU_ERASE;
  cmd->sec_cnt     = count;
  cmd->sec_num     = (sector&0xff);
  cmd->cyl_low     = ((sector>>8 )&0xff);
  cmd->cyl_high    = ((sector>>16)&0xff);
  cmd->drv_headnum = (((sector>>24)&0x0f)|0x40);
  cmd->command     = SPD_CMD_PIO_NODATA;

  retval = hwif_pio_nodata_command(dev);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>hwif_pio_nodata_command() failed(%d)", dev->id+'a', retval);
  }
  return retval;
}


int spd_log_sense(spd_dev_t *dev, u8 page, u8 *buf)
{
  int retval;
  spd_command_t *cmd = &dev->hwif->cmd;
  PTRACE();
  PCOMMAND("<spd%c>LOG SENSE page=%02x", dev->id+'a', page);

  cmd->feature     = SPD_CMD_LOG_SENSE;
  cmd->sec_cnt     = 0;
  cmd->sec_num     = page;
  cmd->cyl_low     = 0;
  cmd->cyl_high    = 0;
  cmd->drv_headnum = 0;
  cmd->command     = SPD_CMD_PIO_DATAIN;

  retval = hwif_pio_datain_command(dev, (void *)buf, 1, 8);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>hwif_pio_datain_command() failed(%d)", dev->id+'a', retval);
    return retval;
  }

  if(unlikely(buf[0] != page)){
    PERROR("<spd%c>log page(%02x) data is wrong(%02x)", dev->id+'a', page, buf[0]);
#if ! defined(SPD_NOCHECK_PAGENUM)
    return -EIO;
#endif /* !SPD_NOCHECK_PAGENUM */
  }

  return 0;
} 


int spd_log_write(spd_dev_t *dev, u8 page, u8 *buf)
{
  int retval;
  spd_command_t *cmd = &dev->hwif->cmd;
  PTRACE();
  PCOMMAND("<spd%c>LOG WRITE page=%02x", dev->id+'a', page);

  cmd->feature     = SPD_CMD_LOG_WRITE;
  cmd->sec_cnt     = 0;
  cmd->sec_num     = page;
  cmd->cyl_low     = 0;
  cmd->cyl_high    = 0;
  cmd->drv_headnum = 0;
  cmd->command     = SPD_CMD_PIO_DATAOUT;

  retval = hwif_pio_dataout_command(dev, (void *)buf, 1, 8);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>hwif_pio_dataout_command() failed(%d)", dev->id+'a', retval);
  }
  return retval;
}


int spd_check_protect(spd_dev_t *dev)
{
  int retval;
  int protect;
  u16 *buf;
  PTRACE();

  buf = (u16 *)dev->tmp_buf;

  retval = spd_identify_device(dev, buf);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>spd_identify_device() failed(%d)", dev->id+'a', retval);
    return retval;
  }

  protect = buf[134];
  return protect;
}


int spd_read_capacity(spd_dev_t *dev)
{
  PTRACE();

  if(spd_is_DEB(dev) && (0 == dev->capacity)){
    int retval = hwif_read_spec(dev);
    if(unlikely(retval < 0)){
      PERROR("<spd%c>hwif_read_spec() failed(%d)", dev->id+'a', retval);
      return 0;
    }
  }

  PINFO("<spd%c>capacity=%d", dev->id+'a', dev->capacity);
  return dev->capacity;
}


int spd_firm_update(spd_dev_t *dev, u8 count, u8 *buf, u8 asel)
{
  int retval;
  spd_command_t *cmd = &dev->hwif->cmd;
  int rcount = count;
  PTRACE();
  PCOMMAND("<spd%c>FIRMWARE UPDATE count=%02x asel=%02x", dev->id+'a', count, asel);

  PINFO("<spd%c>SecCnt=%02xh", dev->id+'a', count);
  if(count != 0x61 && dev->spec_version < 0x31){
    /* spec version before 3.1 */
    PALERT("<spd%c>ALERT:P2card firmware size 61h only but size=%02xh before spec version ver.3.1(%d.%d)", dev->id+'a', count, dev->spec_version>>4, dev->spec_version&0x0F);
  } else if(count == 0){
    /* spec version 3.1- */
    rcount = 256;
    PINFO("<spd%c>Changed real SecCnt %02xh to %02xh", dev->id+'a', count, rcount);
  }

  cmd->feature     = SPD_CMD_FIRMWARE_UPDATE;
  cmd->sec_cnt     = count;
  cmd->sec_num     = 0;
  cmd->cyl_low     = 0;
  cmd->cyl_high    = 0;
  cmd->drv_headnum = asel;
  cmd->command     = SPD_CMD_PIO_DATAOUT;

  retval = hwif_pio_dataout_command(dev, (void *)buf, rcount, 8);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>hwif_pio_dataout_command() failed(%d)", dev->id+'a', retval);
  }
  return retval;
}


int spd_set_power_mode(spd_dev_t *dev, u8 level)
{
  int retval;
  spd_command_t *cmd = &dev->hwif->cmd;
  PTRACE();

  if(level == SPD_PM_NORMAL){
    return 0;
  }
  PCOMMAND("<spd%c>SET POWER MODE level=%02x", dev->id+'a', level);

  cmd->feature     = SPD_CMD_SET_POWER_MODE;
  cmd->sec_cnt     = level;
  cmd->sec_num     = 1;
  cmd->cyl_low     = 0;
  cmd->cyl_high    = 0;
  cmd->drv_headnum = 0;
  cmd->command     = SPD_CMD_SET_FEATURE;

  retval = hwif_pio_nodata_command(dev); 
  if(unlikely(retval < 0)){
    PERROR("<spd%c>hwif_pio_nodata_command() failed(%d)", dev->id+'a', retval);
  }
  return retval;
}


int spd_set_interface_condition(spd_dev_t *dev, u8 condition)
{
  int retval;
  spd_command_t *cmd = &dev->hwif->cmd;
  PTRACE();
  PCOMMAND("<spd%c>SET INTERFACE CONDITION condition=%02x", dev->id+'a', condition);

  cmd->feature     = SPD_CMD_INTERFACE_CONDITION;
  cmd->sec_cnt     = condition;
  cmd->sec_num     = 0;
  cmd->cyl_low     = 0;
  cmd->cyl_high    = 0;
  cmd->drv_headnum = 0;
  cmd->command     = SPD_CMD_PIO_NODATA;

  retval = hwif_pio_nodata_command(dev);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>hwif_pio_nodata_command() failed(%d)", dev->id+'a', retval);
  }
  return retval;
}


int spd_card_initialize(spd_dev_t *dev)
{
  int retval;
  spd_command_t *cmd = &dev->hwif->cmd;
  PTRACE();
  PCOMMAND("<spd%c>CARD INITIALIZE", dev->id+'a');

  cmd->feature     = SPD_CMD_CARD_INITIALIZE;
  cmd->sec_cnt     = 0;
  cmd->sec_num     = 0;
  cmd->cyl_low     = 0;
  cmd->cyl_high    = 0;
  cmd->drv_headnum = 0;
  cmd->command     = SPD_CMD_PIO_NODATA;

  retval = hwif_pio_nodata_command(dev);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>hwif_pio_nodata_command() failed(%d)", dev->id+'a', retval);
  }
  return retval;
}


int spd_card_terminate(spd_dev_t *dev)
{
  int retval;
  unsigned long flags = 0;
  PTRACE();
  PCOMMAND("<spd%c>CARD TERMINATE", dev->id+'a');

  spin_lock_irqsave(&dev->lock, flags);
  if(spd_is_BUSY(dev)){
    PERROR("<spd%c>device busy", dev->id+'a');
    spin_unlock_irqrestore(&dev->lock, flags);
    return -EBUSY;
  }
  spd_set_BUSY(dev);
  hwif_irq_disable(dev);
  spin_unlock_irqrestore(&dev->lock, flags);

  retval = hwif_check_ready(dev);
  if(unlikely(retval < 0)){
    PERROR("hwif_check_ready() failed(%d)", retval);
    spd_clr_BUSY(dev);
    return retval;
  }

  spd_out8(dev, SPD_REG_DHR, 0x00);
  spd_out8(dev, SPD_REG_CT3, 0x02);
  spd_out8(dev, SPD_REG_FTR, 0x01);
  spd_out8(dev, SPD_REG_SCR, 0x00);
  spd_out8(dev, SPD_REG_SNR, 0x00);
  spd_out8(dev, SPD_REG_CLR, 0x00);
  spd_out8(dev, SPD_REG_CHR, 0x00);
  spd_out8(dev, SPD_REG_CMD, SPD_CMD_PIO_DATAIN);
  udelay(1);

  retval = hwif_wait_transfer(dev);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>hwif_wait_transfer() failed(%d)", dev->id+'a', retval);
    spd_clr_BUSY(dev);
  }
  return retval;
}


int spd_card_rescue(spd_dev_t *dev, u8 option)
{
  int retval;
  spd_command_t *cmd = &dev->hwif->cmd;
  PTRACE();
  PCOMMAND("<spd%c>SET RESCUE MODE option=%02x", dev->id+'a', option);

  cmd->feature     = SPD_CMD_CARD_RESCUE;
  cmd->sec_cnt     = option;
  cmd->sec_num     = 0;
  cmd->cyl_low     = 0;
  cmd->cyl_high    = 0;
  cmd->drv_headnum = 0;
  cmd->command     = SPD_CMD_PIO_NODATA;

  retval = hwif_pio_nodata_command(dev);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>hwif_pio_nodata_command() failed(%d)", dev->id+'a', retval);
  }
  return retval;
}


int spd_set_disable_cam_transfer(spd_dev_t *dev)
{
  int retval;
  spd_command_t *cmd = &dev->hwif->cmd;
  PTRACE();
  PCOMMAND("<spd%c>SET Disable Extended CAM Transfer MODE", dev->id+'a');

  cmd->feature     = SPD_CMD_SET_DISABLE_CAM_TRNS;
  cmd->sec_cnt     = 0;
  cmd->sec_num     = 0;
  cmd->cyl_low     = 0;
  cmd->cyl_high    = 0;
  cmd->drv_headnum = 0;
  cmd->command     = SPD_CMD_SET_FEATURE;

  retval = hwif_pio_nodata_command(dev);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>hwif_pio_nodata_command() failed(%d)", dev->id+'a', retval);
  }
  return retval;
}


int spd_table_recover(spd_dev_t *dev, u8 ver)
{
  int retval = 0;
  spd_command_t *cmd = &dev->hwif->cmd;
  PTRACE();
  PCOMMAND("<spd%c>TABLE RECOVER ver=%02x", dev->id+'a', ver);

  cmd->feature     = SPD_CMD_TABLE_RECOVER;
  cmd->sec_cnt     = ver;
  cmd->sec_num     = 0;
  cmd->cyl_low     = 0;
  cmd->cyl_high    = 0;
  cmd->drv_headnum = 0;
  cmd->command     = SPD_CMD_PIO_NODATA;
  retval = hwif_pio_nodata_command(dev);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>hwif_pio_nodata_command() failed(%d)", dev->id+'a', retval);
  }
  return retval;
}


int spd_secured_rec(spd_dev_t *dev, u8 ctrl)
{
  int retval = 0;
  spd_command_t *cmd = &dev->hwif->cmd;
  PTRACE();
  PCOMMAND("<spd%c>SECURED REC ctrl=%02x", dev->id+'a', ctrl);

  cmd->feature     = SPD_CMD_SECURED_REC;
  cmd->sec_cnt     = ctrl;
  cmd->sec_num     = 0;
  cmd->cyl_low     = 0;
  cmd->cyl_high    = 0;
  cmd->drv_headnum = 0;
  cmd->command     = SPD_CMD_PIO_NODATA;
  retval = hwif_pio_nodata_command(dev);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>hwif_pio_nodata_command() failed(%d)", dev->id+'a', retval);
  }
  return retval;

}


int spd_start_rec(spd_dev_t *dev, u8 id)
{
  int retval = 0;
  spd_command_t *cmd = &dev->hwif->cmd;
  PTRACE();
  PCOMMAND("<spd%c>START REC id=%02x", dev->id+'a', id);

  cmd->feature     = SPD_CMD_START_REC;
  cmd->sec_cnt     = id;
  cmd->sec_num     = 0;
  cmd->cyl_low     = 0;
  cmd->cyl_high    = 0;
  cmd->drv_headnum = 0;
  cmd->command     = SPD_CMD_PIO_NODATA;
  retval = hwif_pio_nodata_command(dev);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>hwif_pio_nodata_command() failed(%d)", dev->id+'a', retval);
  }
  return retval;

}


int spd_create_dir(spd_dev_t *dev, u8 id)
{
  int retval = 0;
  spd_command_t *cmd = &dev->hwif->cmd;
  PTRACE();
  PCOMMAND("<spd%c>CREATE DIR id=%02x", dev->id+'a', id);

  cmd->feature     = SPD_CMD_CREATE_DIR;
  cmd->sec_cnt     = id;
  cmd->sec_num     = 0;
  cmd->cyl_low     = 0;
  cmd->cyl_high    = 0;
  cmd->drv_headnum = 0;
  cmd->command     = SPD_CMD_PIO_NODATA;
  retval = hwif_pio_nodata_command(dev);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>hwif_pio_nodata_command() failed(%d)", dev->id+'a', retval);
  }
  return retval;
}


int spd_update_ci(spd_dev_t *dev, u8 id)
{
  int retval = 0;
  spd_command_t *cmd = &dev->hwif->cmd;
  PTRACE();
  PCOMMAND("<spd%c>UPDATE CI id=%02x", dev->id+'a', id);

  cmd->feature     = SPD_CMD_UPDATE_CI;
  cmd->sec_cnt     = id;
  cmd->sec_num     = 0;
  cmd->cyl_low     = 0;
  cmd->cyl_high    = 0;
  cmd->drv_headnum = 0;
  cmd->command     = SPD_CMD_PIO_NODATA;
  retval = hwif_pio_nodata_command(dev);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>hwif_pio_nodata_command() failed(%d)", dev->id+'a', retval);
  }
  return retval;
}


int spd_set_new_au(spd_dev_t *dev, u8 id)
{
  int retval = 0;
  spd_command_t *cmd = &dev->hwif->cmd;
  PTRACE();
  PCOMMAND("<spd%c>SET NEW AU id=%02x", dev->id+'a', id);

  cmd->feature     = SPD_CMD_SET_NEW_AU;
  cmd->sec_cnt     = id;
  cmd->sec_num     = 0;
  cmd->cyl_low     = 0;
  cmd->cyl_high    = 0;
  cmd->drv_headnum = 0;
  cmd->command     = SPD_CMD_PIO_NODATA;
  retval = hwif_pio_nodata_command(dev);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>hwif_pio_nodata_command() failed(%d)", dev->id+'a', retval);
  }
  return retval;
}


int spd_end_rec(spd_dev_t *dev, u8 id)
{
  int retval = 0;
  spd_command_t *cmd = &dev->hwif->cmd;
  PTRACE();
  PCOMMAND("<spd%c>END REC id=%02x", dev->id+'a', id);

  cmd->feature     = SPD_CMD_END_REC;
  cmd->sec_cnt     = id;
  cmd->sec_num     = 0;
  cmd->cyl_low     = 0;
  cmd->cyl_high    = 0;
  cmd->drv_headnum = 0;
  cmd->command     = SPD_CMD_PIO_NODATA;
  retval = hwif_pio_nodata_command(dev);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>hwif_pio_nodata_command() failed(%d)", dev->id+'a', retval);
  }
  return retval;
}


int spd_sd_device_reset(spd_dev_t *dev)
{
  int retval = 0;
  spd_command_t *cmd = &dev->hwif->cmd;
  PTRACE();
  PCOMMAND("<spd%c>DEVICE RESET", dev->id+'a');

  cmd->feature     = SPD_CMD_SD_DEV_RESET;
  cmd->sec_cnt     = 0;
  cmd->sec_num     = 0;
  cmd->cyl_low     = 0;
  cmd->cyl_high    = 0;
  cmd->drv_headnum = 0;
  cmd->command     = SPD_CMD_PIO_NODATA;
  retval = hwif_pio_nodata_command(dev);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>hwif_pio_nodata_command() failed(%d)", dev->id+'a', retval);
  }
  return retval;
}


int spd_go_hibernate(spd_dev_t *dev)
{
  int retval = 0;
  spd_command_t *cmd = &dev->hwif->cmd;
  PTRACE();
  PCOMMAND("<spd%c>GO HIBERNATE", dev->id+'a');

  cmd->feature     = SPD_CMD_GO_HIBERNATE;
  cmd->sec_cnt     = 0;
  cmd->sec_num     = 0;
  cmd->cyl_low     = 0;
  cmd->cyl_high    = 0;
  cmd->drv_headnum = 0;
  cmd->command     = SPD_CMD_PIO_NODATA;
  retval = hwif_pio_nodata_command(dev);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>hwif_pio_nodata_command() failed(%d)", dev->id+'a', retval);
  }
  return retval;
}


static inline void hwif_cmd_set_sdcmd_arg(spd_command_t *cmd, u32 arg)
{
  cmd->sec_cnt  = (u8)arg;
  cmd->sec_num  = (u8)(arg >> 8);
  cmd->cyl_low  = (u8)(arg >> 16);
  cmd->cyl_high = (u8)(arg >> 24);
}


int spd_lmg(spd_dev_t *dev, u32 arg, u8 *buf)
{
  int retval = 0;
  spd_command_t *cmd = &dev->hwif->cmd;
  PTRACE();
  PCOMMAND("<spd%c>LMG arg=%08x", dev->id+'a', arg);

  DUMP_DATA(buf, 16);

  cmd->feature     = SPD_CMD_LMG;
  cmd->drv_headnum = 0;
  cmd->command     = SPD_CMD_PIO_DATAOUT;
  hwif_cmd_set_sdcmd_arg(cmd, arg);
  retval = hwif_pio_dataout_command(dev, (void *)buf, 1, 8);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>hwif_pio_dataout_command() failed(%d)", dev->id+'a', retval);
  }
  return retval;
}


int spd_get_sst(spd_dev_t *dev, u8 *buf)
{
  int retval = 0;
  spd_command_t *cmd = &dev->hwif->cmd;
  PTRACE();
  PCOMMAND("<spd%c>GET SST", dev->id+'a');

  cmd->feature     = SPD_CMD_GET_SD_STA;
  cmd->sec_cnt     = 0;
  cmd->sec_num     = 0x0d;
  cmd->cyl_low     = 0;
  cmd->cyl_high    = 0;
  cmd->drv_headnum = 0;
  cmd->command     = SPD_CMD_PIO_DATAIN;
  retval = hwif_pio_datain_command(dev, (void *)buf, 1, 8);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>hwif_pio_datain_command() failed(%d)", dev->id+'a', retval);
  }
  return retval;
}


int spd_get_dinfo(spd_dev_t *dev, u32 arg, u8 *buf)
{
  int retval = 0;
  spd_command_t *cmd = &dev->hwif->cmd;
  PTRACE();
  PCOMMAND("<spd%c>GET DINFO arg=%08x", dev->id+'a', arg);

  cmd->feature     = SPD_CMD_GET_DINFO;
  cmd->drv_headnum = 0;
  cmd->command     = SPD_CMD_PIO_DATAIN;
  hwif_cmd_set_sdcmd_arg(cmd, arg);
  retval = hwif_pio_datain_command(dev, (void *)buf, 1, 8);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>hwif_pio_datain_command() failed(%d)", dev->id+'a', retval);
  }
  return retval;
}


int spd_dmg(spd_dev_t *dev, u32 arg, u8 *buf)
{
  int retval = 0;
  spd_command_t *cmd = &dev->hwif->cmd;
  PTRACE();
  PCOMMAND("<spd%c>DMG arg=%08x", dev->id+'a', arg);

  DUMP_DATA(buf, 32);

  cmd->feature     = SPD_CMD_DMG;
  cmd->drv_headnum = 0;
  cmd->command     = SPD_CMD_PIO_DATAOUT;
  hwif_cmd_set_sdcmd_arg(cmd, arg);
  retval = hwif_pio_dataout_command(dev, (void *)buf, 1, 8);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>hwif_pio_dataout_command() failed(%d)", dev->id+'a', retval);
  }
  return retval;
}


int spd_esw(spd_dev_t *dev, u8 sw)
{
  int retval = 0;
  spd_command_t *cmd = &dev->hwif->cmd;
  unsigned long flags = 0;
  PTRACE();
  PCOMMAND("<spd%c>ESW sw=%08x", dev->id+'a', sw);

  cmd->feature     = SPD_CMD_ESW;
  cmd->sec_cnt     = sw;
  cmd->sec_num     = 0;
  cmd->cyl_low     = 0;
  cmd->cyl_high    = 0;
  cmd->drv_headnum = 0;
  cmd->command     = SPD_CMD_PIO_NODATA;
  retval = hwif_pio_nodata_command(dev);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>hwif_pio_nodata_command() failed(%d)", dev->id+'a', retval);
    return retval;
  }

  spin_lock_irqsave(&dev->lock, flags);
  if(spd_is_LK(dev)){
    PINFO("<spd%c>set features", dev->id+'a');
    retval = hwif_set_features(dev, (dev->spec_version>>4));
    if(unlikely(retval < 0)){
      PERROR("<spd%c>hwif_set_features() failed(%d)", dev->id+'a', retval);
      spin_unlock_irqrestore(&dev->lock, flags);
      return (retval);
    }
  }
  spin_unlock_irqrestore(&dev->lock, flags);

  return 0;
}


int spd_get_ph(spd_dev_t *dev, u32 arg, u8 *buf)
{
  int retval = 0;
  spd_command_t *cmd = &dev->hwif->cmd;
  PTRACE();
  PCOMMAND("<spd%c>GET PH arg=%08x", dev->id+'a', arg);

  cmd->feature     = SPD_CMD_GET_PH;
  cmd->drv_headnum = 0;
  cmd->command     = SPD_CMD_PIO_DATAIN;
  hwif_cmd_set_sdcmd_arg(cmd, arg);
  retval = hwif_pio_datain_command(dev, (void *)buf, 1, 8);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>hwif_pio_datain_command() failed(%d)", dev->id+'a', retval);
  }
  return retval;
}


int spd_get_linfo(spd_dev_t *dev, u32 arg, u8 *buf)
{
  int retval = 0;
  spd_command_t *cmd = &dev->hwif->cmd;
  PTRACE();
  PCOMMAND("<spd%c>GET LINFO arg=%08x", dev->id+'a', arg);

  cmd->feature     = SPD_CMD_GET_LINFO;
  cmd->drv_headnum = 0;
  cmd->command     = SPD_CMD_PIO_DATAIN;
  hwif_cmd_set_sdcmd_arg(cmd, arg);
  retval = hwif_pio_datain_command(dev, (void *)buf, 1, 8);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>hwif_pio_datain_command() failed(%d)", dev->id+'a', retval);
  }
  return retval;
}


int spd_dau(spd_dev_t *dev, u32 arg, u8 *buf)
{
  int retval = 0;
  spd_command_t *cmd = &dev->hwif->cmd;
  PTRACE();
  PCOMMAND("<spd%c>DAU arg=%08x", dev->id+'a', arg);

  DUMP_DATA(buf, 32);

  cmd->feature     = SPD_CMD_DAU;
  cmd->drv_headnum = 0;
  cmd->command     = SPD_CMD_PIO_DATAOUT;
  hwif_cmd_set_sdcmd_arg(cmd, arg);
  retval = hwif_pio_dataout_command(dev, (void *)buf, 1, 8);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>hwif_pio_dataout_command() failed(%d)", dev->id+'a', retval);
  }
  return retval;
}


int spd_set_dparam(spd_dev_t *dev, u32 arg, u8 *buf)
{
  int retval = 0;
  spd_command_t *cmd = &dev->hwif->cmd;
  PTRACE();
  PCOMMAND("<spd%c>SET DPARAM arg=%08x", dev->id+'a', arg);

  DUMP_DATA(buf, 32);

  cmd->feature     = SPD_CMD_SET_DPARAM;
  cmd->drv_headnum = 0;
  cmd->command     = SPD_CMD_PIO_DATAOUT;
  hwif_cmd_set_sdcmd_arg(cmd, arg);
  retval = hwif_pio_dataout_command(dev, (void *)buf, 1, 8);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>hwif_pio_dataout_command() failed(%d)", dev->id+'a', retval);
  }
  return retval;
}


int spd_get_phs(spd_dev_t *dev, u32 arg, u8 *buf)
{
  int retval = 0;
  spd_command_t *cmd = &dev->hwif->cmd;
  PTRACE();
  PCOMMAND("<spd%c>GET PHS arg=%08x", dev->id+'a', arg);

  cmd->feature     = SPD_CMD_GET_PHS;
  cmd->drv_headnum = 0;
  cmd->command     = SPD_CMD_PIO_DATAIN;
  hwif_cmd_set_sdcmd_arg(cmd, arg);
  cmd->cyl_low     = 0;
  cmd->cyl_high    = 0;
  retval = hwif_pio_datain_command(dev, (void *)buf, 1, 8);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>hwif_pio_datain_command() failed(%d)", dev->id+'a', retval);
  }
  return retval;
}
