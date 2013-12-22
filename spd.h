/*
 P2card driver common header
 $Id: spd.h 21639 2013-04-09 00:15:24Z Yasuyuki Matsumoto $
 */
#ifndef _SPD_H
#define _SPD_H

#define SPD_VERSION	 "2.6.2.4"
#include "spd_ioctl.h"

#ifdef __KERNEL__
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/wait.h>
#include <linux/completion.h>
#include <linux/interrupt.h>
#include <linux/blkdev.h>
#include <linux/genhd.h>
#include <linux/hdreg.h>
#include <linux/cdev.h>
#include <linux/version.h>
#include <asm/atomic.h>

#if (KERNEL_VERSION(2,6,27) <= LINUX_VERSION_CODE) /* KERNEL_VERSION : 2.6.27 - */
# include <linux/semaphore.h>
#else /* KERNEL_VERSION : - 2.6.26 */
# include <asm/semaphore.h>
#endif /* KERNEL_VERSION : 2.6.27 - */


#if defined(CONFIG_P2PF_K230)
#include "arch/defs-K230.h"
#elif defined(CONFIG_P2PF_K250)
#include "arch/defs-K250.h"
#elif defined(CONFIG_P2PF_K202)
#include "arch/defs-K202.h"
#elif defined(CONFIG_P2PF_K240)
#include "arch/defs-K240.h"
#elif defined(CONFIG_P2PF_K246A)
#include "arch/defs-K246A.h"
#elif defined(CONFIG_P2PF_E51X)
#include "arch/defs-E51X.h"
#elif defined(CONFIG_P2PF_E522)
#include "arch/defs-E522.h"
#elif defined(CONFIG_P2PF_K200)
#include "arch/defs-K200.h"
#elif defined(CONFIG_P2PF_K220)
#include "arch/defs-K220.h"
#elif defined(CONFIG_P2PF_K251)
#include "arch/defs-K251.h"
#elif defined(CONFIG_P2PF_K277)
#include "arch/defs-K277.h"
#elif defined(CONFIG_P2PF_K286)
#include "arch/defs-K286.h"
#elif defined(CONFIG_P2PF_K298)
#include "arch/defs-K298.h"
#elif defined(CONFIG_P2PF_K302)
#include "arch/defs-K302.h"
#elif defined(CONFIG_P2PF_E605)
#include "arch/defs-E605.h"
#elif defined(CONFIG_P2PF_K292)
#include "arch/defs-K292.h"
#elif defined(CONFIG_P2PF_K301)
#include "arch/defs-K301.h"
#elif defined(CONFIG_P2PF_K283)
#include "arch/defs-K283.h"
#elif defined(CONFIG_P2PF_K318)
#include "arch/defs-K318.h"
#elif defined(CONFIG_P2PF_K327)
#include "arch/defs-K327.h"
#elif defined(CONFIG_P2PF_K329)
#include "arch/defs-K329.h"
#elif defined(CONFIG_P2PF_SAV8313_K200)
#include "arch/defs-SAV8313_K200.h"
#elif defined(CONFIG_P2PF_SAV8313_K230)
#include "arch/defs-SAV8313_K230.h"
#elif defined(CONFIG_P2PF_MPC83XXBRB)
#include "arch/defs-MPC83XXBRB.h"
#elif defined(CONFIG_P2PF_MSU01)
#include "arch/defs-MSU01.h"
#elif defined(CONFIG_P2PF_BRB01)
#include "arch/defs-BRB01.h"
#else  /* CONFIG_P2PF_XXX */
#include "arch/defs-generic.h"
#endif /* CONFIG_P2PF_XXX */

#line __LINE__ "spd.h" /* Replace full path(__FILE__) to spd.h. */


typedef struct _spd_area_info_t {
  u32 start;
  u32 end;
  u32 wsize;
  u32 ausize;
} spd_area_info_t;


enum SPD_DEFAULT_ENUM {
  SPD_HARDSECT_SIZE        = 512,
  SPD_N_AREA               = 2,
  SPD_N_RETRY              = 4,
  SPD_NO_RETRY             = 0,
  SPD_DMA_TIMEOUT          = (1*HZ),
  SPD_CMD_TIMEOUT          = (3*HZ + HZ/2),
  SPD_CMD_LONG_TIMEOUT     = (15*HZ),
  SPD_CMD_LONGLONG_TIMEOUT = (40*HZ),

  SPD_CARD_ID_SIZE  = 16,
  SPD_MODEL_ID_SIZE = 32,

  SPD_SG_N_ENTRY   = 8192,
  SPD_SG_SIZE      = sizeof(spd_scatterlist_t) * SPD_SG_N_ENTRY,
  SPD_SG_MAX_COUNT = 0x10000, /* 64KB -> 64KB x 8192 = 512MB */
  SPD_SG_ENDMARK   = 0x80000000,

  SPD_CACHE_BUFFER_ORDER = 7,
  SPD_CACHE_BUFFER_SIZE  = PAGE_SIZE * 128,
  /* SPD_CACHE_N_BUFFER is defined at arch/defs-XXX.h */
  SPD_CACHE_MAX_SIZE     = SPD_CACHE_BUFFER_SIZE * SPD_CACHE_N_BUFFER,
  SPD_CACHE_SG_N_ENTRY   = SPD_CACHE_MAX_SIZE / SPD_SG_MAX_COUNT,
  SPD_CACHE_SG_SIZE      = SPD_CACHE_SG_N_ENTRY * sizeof(spd_scatterlist_t),

  SPD_WSIZE_16K  = 0x20,
  SPD_WSIZE_64K  = 0x80,
  SPD_WSIZE_512K = 0x400,
  SPD_WSIZE_2M   = 0x1000,
  SPD_WSIZE_4M   = 0x2000,
  SPD_WSIZE_16M  = 0x8000,

  SPD_DIR_READ  = 0x00,
  SPD_DIR_WRITE = 0x01,

  SPD_TARGET_LOCAL = 0,
  SPD_TARGET_ZION  = 1,
  SPD_TARGET_FPGA  = 2,
  SPD_TARGET_DYNA  = 3,

  /* for Interface Condition */
  SPD_IO_LOCK   = 0x00,
  SPD_IO_UNLOCK = 0x01,
};


enum SPD_STATUS_BIT_ENUM {
  /* for hwif */
  SPD_IO_ENABLE      = (1<<0),
  SPD_IO_BUSY        = (1<<1),
  SPD_DMA_BUSY       = (1<<2),
  SPD_CARD_PRESENT   = (1<<3),
  SPD_CARD_UNSUPPORT = (1<<4),
  SPD_WRITE_GUARD    = (1<<5),
  
  /* for rdev */
  SPD_CARD_ERROR  = (1<<8),
  SPD_RICOH_ERROR = (1<<9),
  
  /* for bdev */
  SPD_DISK_ENABLE     = (1<<16),
  SPD_DISK_OPEN       = (1<<17),
  SPD_DWM_ENABLE      = (1<<18),
  SPD_REQUEST_PENDING = (1<<19),
  SPD_DISK_ADD_REQ    = (1<<20),

  /* for adpt */
  SPD_ADPT_LK         = (1<<24),
  SPD_ADPT_DEB        = (1<<25),
  SPD_ADPT_AUT_ERROR  = (1<<26),
};


enum SPD_EVENT_BIT_ENUM {
  /* sender:hwif */
  SPD_EVENT_CARD_INSERT    = (1<< 0),
  SPD_EVENT_CARD_REMOVE    = (1<< 1),
  SPD_EVENT_CARD_READY     = (1<< 2),
  SPD_EVENT_CARD_ERROR     = (1<< 3),
  SPD_EVENT_CARD_UNSUPPORT = (1<< 4),
  SPD_EVENT_RICOH_ERROR    = (1<< 5),

  /* sender:bdev */
  SPD_EVENT_DISK_ENABLE  = (1<< 8),
  SPD_EVENT_DISK_OPEN    = (1<< 9),
  SPD_EVENT_DISK_CLOSE   = (1<< 10),
  SPD_EVENT_DISK_DISABLE = (1<< 11),

  /* sender:rdev */
  SPD_EVENT_DIRECT_COMPLETE = (1<< 12),

  /* sender:all */
  SPD_EVENT_IO_LOCKED   = (1<< 16),
  SPD_EVENT_IO_UNLOCKED = (1<< 17),

  SPD_EVENT_FATAL_ERROR = (1<< 31),
};


enum SPD_AUT_STATUS_BIT_ENUM {
  SPD_AUT_STATUS_ERROR  = (1<< 0),
};


typedef struct _spd_dev_t {
  struct device *dev;
  int id;
  dev_t devnum;

  spinlock_t lock;
  struct semaphore io_sem;

  atomic_t status;

  u8 spec_version;
  u8 is_p2;
  u8 is_up2;
  u8 is_lk;
  u8 is_over;
  int capacity;
  spd_area_info_t area[SPD_N_AREA];
  int n_area;
  u32 dma_timeout;
  int dma_retry;

  int errcode;
  int retry;
  unsigned long time_stamp;
  unsigned long ticks;
  unsigned long timeout;
  void (*complete_handler)(struct _spd_dev_t *dev);  

  spd_scatterlist_t *sg;
  int sg_n_entry;
  void *tmp_buf;
  struct _spd_cache_t *cache;

  struct _spd_hwif_private_t *hwif;
  struct _spd_bdev_private_t *bdev;
  struct _spd_rdev_private_t *rdev;
  struct _spd_udev_private_t *udev;

  struct work_struct wq; /* bdev.c */
  struct p2_directw_list *directw_list; /* adpt.c */
} spd_dev_t;


typedef struct _spd_cache_t {
  struct list_head list;
  int id;
  spd_dev_t *owner;
  atomic_t status;
  u32 n_sector;
  u32 sector;
  u32 write_size;
  void *buffer[SPD_CACHE_N_BUFFER];
  spd_scatterlist_t *sg;
} spd_cache_t;


enum SPD_CACHE_ENUM {
  SPD_CACHE_DIRTY = (1<<0),
};


/* spd.c */
extern spd_dev_t spd_dev[SPD_N_DEV];
void spd_send_event(spd_dev_t *dev, u32 event, unsigned long arg);
spd_cache_t *spd_cache_alloc(spd_dev_t *dev);
void spd_cache_release(spd_dev_t *dev, spd_cache_t *cache);
void spd_cache_invalidate(spd_dev_t *dev);


/* spd_hwif.c */
int spd_read_sector(spd_dev_t *dev, u32 sector, u16 count, spd_scatterlist_t *sg);
int spd_write_sector(spd_dev_t *dev, u32 sector, u16 count, spd_scatterlist_t *sg);
int spd_seq_write_sector(spd_dev_t *dev,u32 sector,u16 count, spd_scatterlist_t *sg);
int spd_identify_device(spd_dev_t *dev, u16 *buf);
int spd_sector_erase(spd_dev_t *dev, u32 sector, u8 count);
int spd_block_erase(spd_dev_t *dev, u32 sector, u8 count);
int spd_log_sense(spd_dev_t *dev, u8 page, u8 *buf);
int spd_log_write(spd_dev_t *dev, u8 page, u8 *buf);
int spd_check_protect(spd_dev_t *dev);
int spd_read_capacity(spd_dev_t *dev);
int spd_firm_update(spd_dev_t *dev, u8 count, u8 *buf, u8 asel);
int spd_set_power_mode(spd_dev_t *dev, u8 level);
int spd_set_interface_condition(spd_dev_t *dev, u8 condition);
int spd_card_initialize(spd_dev_t *dev);
int spd_card_terminate(spd_dev_t *dev);
int spd_card_rescue(spd_dev_t *dev, u8 option);
int spd_set_disable_cam_transfer(spd_dev_t *dev);

int spd_table_recover(spd_dev_t *dev, u8 ver);
int spd_secured_rec(spd_dev_t *dev, u8 ctrl);
int spd_start_rec(spd_dev_t *dev, u8 id);
int spd_create_dir(spd_dev_t *dev, u8 id);
int spd_update_ci(spd_dev_t *dev, u8 id);
int spd_set_new_au(spd_dev_t *dev, u8 id);
int spd_end_rec(spd_dev_t *dev, u8 id);
int spd_sd_device_reset(spd_dev_t *dev);
int spd_go_hibernate(spd_dev_t *dev);
int spd_au_erase(spd_dev_t *dev, u32 sector, u8 count);

int spd_lmg(spd_dev_t *dev, u32 arg, u8 *buf);
int spd_get_sst(spd_dev_t *dev, u8 *buf);
int spd_get_dinfo(spd_dev_t *dev, u32 arg, u8 *buf);
int spd_dmg(spd_dev_t *dev, u32 arg, u8 *buf);
int spd_esw(spd_dev_t *dev, u8 sw);
int spd_get_ph(spd_dev_t *dev, u32 arg, u8 *buf);
int spd_get_linfo(spd_dev_t *dev, u32 arg, u8 *buf);
int spd_dau(spd_dev_t *dev, u32 arg, u8 *buf);
int spd_set_dparam(spd_dev_t *dev, u32 arg, u8 *buf);
int spd_get_phs(spd_dev_t *dev, u32 arg, u8 *buf);


/* spd_adpt.c */
int spd_adpt_fsmi_write(spd_dev_t *dev, unsigned long arg);
int spd_adpt_sdcmd_write(spd_dev_t *dev, unsigned long arg);
int spd_adpt_read_modify_write(spd_dev_t *dev, unsigned long arg);


#define  DBG_SYS_NAME "[spd]"
#include "debug.h"

static inline void spd_set_status(spd_dev_t *dev, int mask)
{
#if !defined(CONFIG_PPC32) && !defined(CONFIG_PPC64)
  atomic_set_mask(mask, &dev->status);
#else /* CONFIG_PPC32 */
  unsigned long flags = 0;
  local_irq_save(flags);
  dev->status.counter |= mask;
  local_irq_restore(flags);
#endif /* !CONFIG_PPC32 && !CONFIG_PPC64 */

  PINFO("<spd%c>set status=%d", dev->id+'a', ffs(mask)-1);
}


static inline void spd_clr_status(spd_dev_t *dev, int mask)
{
#if !defined(CONFIG_PPC32) && !defined(CONFIG_PPC64)
  atomic_clear_mask(mask, &dev->status);
#else /* !CONFIG_PPC32 */
  unsigned long flags = 0;
  local_irq_save(flags);
  dev->status.counter &= ~mask;
  local_irq_restore(flags);
#endif /* !CONFIG_PPC32 && !CONFIG_PPC64 */

  PINFO("<spd%c>clr status=%d", dev->id+'a', ffs(mask)-1);
}

static inline int spd_is_status(spd_dev_t *dev, int mask)
{
  return (atomic_read(&dev->status)&mask);
}


#define spd_set_IOE(dev)  spd_set_status((dev), SPD_IO_ENABLE)
#define spd_clr_IOE(dev)  spd_clr_status((dev), SPD_IO_ENABLE)
#define spd_is_IOE(dev)   spd_is_status((dev),  SPD_IO_ENABLE)
#define spd_set_BUSY(dev) spd_set_status((dev), SPD_IO_BUSY)
#define spd_clr_BUSY(dev) spd_clr_status((dev), SPD_IO_BUSY)
#define spd_is_BUSY(dev)  spd_is_status((dev),  SPD_IO_BUSY)
#define spd_set_DMA(dev)  spd_set_status((dev), SPD_DMA_BUSY)
#define spd_clr_DMA(dev)  spd_clr_status((dev), SPD_DMA_BUSY)
#define spd_is_DMA(dev)   spd_is_status((dev),  SPD_DMA_BUSY)
#define spd_set_DWM(dev)  spd_set_status((dev), SPD_DWM_ENABLE)
#define spd_clr_DWM(dev)  spd_clr_status((dev), SPD_DWM_ENABLE)
#define spd_is_DWM(dev)   spd_is_status((dev),  SPD_DWM_ENABLE)
#define spd_set_GDE(dev)  spd_set_status((dev), SPD_DISK_ENABLE)
#define spd_clr_GDE(dev)  spd_clr_status((dev), SPD_DISK_ENABLE)
#define spd_is_GDE(dev)   spd_is_status((dev),  SPD_DISK_ENABLE)
#define spd_set_DAR(dev)  spd_set_status((dev), SPD_DISK_ADD_REQ)
#define spd_clr_DAR(dev)  spd_clr_status((dev), SPD_DISK_ADD_REQ)
#define spd_is_DAR(dev)   spd_is_status((dev),  SPD_DISK_ADD_REQ)

#define spd_set_LK(dev)   spd_set_status((dev), SPD_ADPT_LK)
#define spd_clr_LK(dev)   spd_clr_status((dev), SPD_ADPT_LK)
#define spd_is_LK(dev)    spd_is_status((dev),  SPD_ADPT_LK)
#define spd_set_DEB(dev)  spd_set_status((dev), SPD_ADPT_DEB)
#define spd_clr_DEB(dev)  spd_clr_status((dev), SPD_ADPT_DEB)
#define spd_is_DEB(dev)   spd_is_status((dev),  SPD_ADPT_DEB)
#define spd_set_AUE(dev)  spd_set_status((dev), SPD_ADPT_AUT_ERROR)
#define spd_clr_AUE(dev)  spd_clr_status((dev), SPD_ADPT_AUT_ERROR)
#define spd_is_AUE(dev)   spd_is_status((dev),  SPD_ADPT_AUT_ERROR)


static inline int spd_io_lock(spd_dev_t *dev)
{
  int retval;
  retval = down_interruptible(&dev->io_sem);
  if(retval >= 0){
    spd_send_event(dev, SPD_EVENT_IO_LOCKED, 0);
  }
  return retval;
}


static inline int spd_io_trylock(spd_dev_t *dev)
{
  int retval;
  retval = down_trylock(&dev->io_sem);
  if(retval == 0){
    spd_send_event(dev, SPD_EVENT_IO_LOCKED, 1);
  }
  
  return retval;
}


static inline void spd_io_unlock(spd_dev_t *dev)
{
  up(&dev->io_sem);
  spd_send_event(dev, SPD_EVENT_IO_UNLOCKED, 0);
}


static inline void spd_cache_set_dirty(spd_cache_t *cache)
{
  if(cache){
#if !defined(CONFIG_PPC32) && !defined(CONFIG_PPC64)
    atomic_set_mask(SPD_CACHE_DIRTY, &cache->status);
#else /* CONFIG_PPC32 */
    unsigned long flags = 0;
    local_irq_save(flags);
    cache->status.counter |= SPD_CACHE_DIRTY;
    local_irq_restore(flags);
#endif /* !CONFIG_PPC32 && !CONFIG_PPC64 */

  } else {
    PERROR("cache is null");
  }
}


static inline void spd_cache_clr_dirty(spd_cache_t *cache)
{
  if(cache){
#if !defined(CONFIG_PPC32) && !defined(CONFIG_PPC64)
    atomic_clear_mask(SPD_CACHE_DIRTY, &cache->status);
#else /* CONFIG_PPC32 */
    unsigned long flags = 0;
    local_irq_save(flags);
    cache->status.counter &= ~(SPD_CACHE_DIRTY);
    local_irq_restore(flags);
#endif /* !CONFIG_PPC32 && !CONFIG_PPC64 */
  } else {
    PERROR("cache is null");
  }
}


static inline int spd_cache_is_dirty(spd_cache_t *cache)
{
  if(cache){
    return (atomic_read(&cache->status)&SPD_CACHE_DIRTY);
  } else {
    PERROR("cache is null");
    return 0;
  }
}


#if (KERNEL_VERSION(2,6,20) <= LINUX_VERSION_CODE) /* KERNEL_VERSION : 2.6.20 - */
# define spd_dma_cache_wback(dev, vaddr, size) dma_cache_sync(dev, vaddr, size, DMA_TO_DEVICE)
# define spd_dma_cache_inv(dev, vaddr, size) dma_cache_sync(dev, vaddr, size, DMA_FROM_DEVICE)
#else /* KERNEL_VERSION : - 2.6.19 */
# define spd_dma_cache_wback(dev, vaddr, size) dma_cache_wback(vaddr, size)
# define spd_dma_cache_inv(dev, vaddr, size) dma_cache_inv(vaddr, size)
#endif /* KERNEL_VERSION : 2.6.20 - */

static inline void spd_cache_prepare(spd_dev_t *dev, int io_dir)
{
  spd_cache_t *cache = dev->cache;
  int end_entry = (cache->n_sector*SPD_HARDSECT_SIZE)/SPD_SG_MAX_COUNT;

  /* clear end mark */
  cache->sg[ 0].count &= cpu_to_le32(~SPD_SG_ENDMARK);    /*  64KB */
  cache->sg[ 7].count &= cpu_to_le32(~SPD_SG_ENDMARK);    /* 512KB */
  cache->sg[63].count &= cpu_to_le32(~SPD_SG_ENDMARK);    /*   4MB */
  if(SPD_CACHE_SG_N_ENTRY > 255)
    cache->sg[ 255].count &= cpu_to_le32(~SPD_SG_ENDMARK); /* 16MB */
  if(SPD_CACHE_SG_N_ENTRY > 1023)
    cache->sg[1023].count &= cpu_to_le32(~SPD_SG_ENDMARK); /* 64MB */
  
  /* set end mark */
  end_entry = (end_entry > 0) ? end_entry-1 : 0;
  PINFO("%d / %d = %d", cache->n_sector, SPD_SG_MAX_COUNT, end_entry);
  cache->sg[end_entry].count |= cpu_to_le32(SPD_SG_ENDMARK);

  spd_dma_cache_wback(dev->dev, cache->sg, SPD_CACHE_SG_SIZE);

  switch(cache->n_sector){
  case SPD_WSIZE_16K:
  case SPD_WSIZE_64K:
    {
      if(io_dir == SPD_DIR_READ){
        spd_dma_cache_inv(dev->dev, cache->buffer[0], cache->n_sector*SPD_HARDSECT_SIZE);
      } else {
        spd_dma_cache_wback(dev->dev, cache->buffer[0], cache->n_sector*SPD_HARDSECT_SIZE);
      }
      break;
    }

  case SPD_WSIZE_512K:
  case SPD_WSIZE_4M:
  case SPD_WSIZE_16M:
    {
      int i = 0;
      int num = (cache->n_sector * SPD_HARDSECT_SIZE) / SPD_CACHE_BUFFER_SIZE;

      for(i = 0; i < num; i++){
	if(io_dir == SPD_DIR_READ){
	  spd_dma_cache_inv(dev->dev, cache->buffer[i], SPD_CACHE_BUFFER_SIZE);
	} else {
	  spd_dma_cache_wback(dev->dev, cache->buffer[i], SPD_CACHE_BUFFER_SIZE);
	}
      }
      break;
    }

  default:
    {
      PERROR("unknown cache size (n_sector = %04x)", cache->n_sector);
    }
  }

  PRINT_SG(cache->sg, end_entry+1, dev->id);
}


static inline void spd_cache_copy(spd_cache_t *cache, u32 offset, u8 *p, u32 size)
{
  int n;
  int b_offset, b_size;

  n        = offset/SPD_CACHE_BUFFER_SIZE;
  b_offset = offset%SPD_CACHE_BUFFER_SIZE;
  b_size   = SPD_CACHE_BUFFER_SIZE - b_offset;

  if(b_size >= size){
    memcpy(cache->buffer[n]+b_offset, p, size);
    return;
  }

  memcpy(cache->buffer[n]+b_offset, p, b_size);
  p    += b_size;
  size -= b_size;
  n++;

  while(size > 0){
    b_size = (SPD_CACHE_BUFFER_SIZE>size)?size:SPD_CACHE_BUFFER_SIZE;
    memcpy(cache->buffer[n], p, b_size);
    p    += b_size;
    size -= b_size;
    n++;
  }
}


static inline int spd_get_wsize(spd_dev_t *dev, u32 sector)
{
  int i;
  spd_area_info_t *area;
  for(i = 0; i < dev->n_area; i++){
    area = &dev->area[i];
    if(sector >= area->start && sector <= area->end){
      return area->wsize;
    }
  }
  PERROR("<spd%c>area info not found. sector=0x%08x", dev->id+'a', sector);

  return -EINVAL;
}


static inline u32 spd_get_sector_offset(spd_dev_t *dev, u32 sector)
{
  int i;
  spd_area_info_t *area;
  for(i = 0; i < dev->n_area; i++){
    area = &dev->area[i];
    if(sector >= area->start && sector <= area->end){
      return area->start;
    }
  }
  PERROR("<spd%c>area info not found. sector=0x%08x", dev->id+'a', sector);

  return ~0L;
}


static inline u32 spd_sdsta2au(u8 sta)
{
  switch(sta){
  case 0x00: return 0;
  case 0x0B: return 0x6000;
  case 0x0C: return 0x8000;
  case 0x0D: return 0xC000;
  case 0x0E: return 0x10000;
  case 0x0F: return 0x20000;
  default: return (0x20 << (sta-1));
  }
}


#endif /* __KERNEL__ */
#endif /* _SPD_H */
