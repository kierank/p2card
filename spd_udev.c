/*
 P2Card USB driver modules
 $Id:$
 */
static int udev_major;
#define MAJOR_NR          udev_major
#define DEVICE_NR(device) MINOR(device)
#define DEVICE_NAME       "spdu"

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/poll.h>
#include <linux/cdev.h>
#include <asm/uaccess.h>

#include "spd.h"
#include "spd_udev.h"

#line __LINE__ "spd_udev.c" /* Replace full path(__FILE__) to spd_udev.c. */

static void udev_event_handler(spd_dev_t *dev, u32 event, unsigned long arg);
static int  udev_open(struct inode *inode, struct file *file);
static int  udev_release(struct inode *inode, struct file *file);
static int  udev_ioctl(struct inode *inode, struct file *file,
		       unsigned int cmd, unsigned long arg);
static unsigned int udev_poll(struct file *file,
			      struct poll_table_struct *poll_table);

static int  ioc_udev_check_power_mode(spd_dev_t *dev, unsigned long arg);
static int  ioc_udev_identify_device(spd_dev_t *dev, unsigned long arg);
static int  ioc_udev_read_sector(spd_dev_t *dev, unsigned long arg);
static int  ioc_udev_write_sector(spd_dev_t *dev, unsigned long arg);
static int  ioc_udev_get_status(spd_dev_t *dev, unsigned long arg);
static int  ioc_udev_log_sense(spd_dev_t *dev, unsigned long arg);
static int  ioc_udev_log_write(spd_dev_t *dev, unsigned long arg);
static int  ioc_udev_block_erase(spd_dev_t *dev, unsigned long arg);
static int  ioc_udev_sector_erase(spd_dev_t *dev, unsigned long arg);
static int  ioc_udev_firm_update(spd_dev_t *dev, unsigned long arg);

static spd_udev_t spd_udev[SPD_N_DEV];

static struct file_operations fops = {
  .owner   = THIS_MODULE,
  .open    = udev_open,
  .release = udev_release,
  .ioctl   = udev_ioctl,
  .poll    = udev_poll,
};


int spd_udev_init(void)
{
  int i;
  int retval;
  dev_t id;
  spd_dev_t *dev;
  PTRACE();

  memset(spd_udev, 0, sizeof(spd_udev));
  for(i = 0; i < SPD_N_DEV; i++){
    dev                        = &spd_dev[i];
    dev->udev                  = &spd_udev[i];
    dev->udev->event_mask      = 0;
    dev->udev->event_handler   = udev_event_handler;
    dev->udev->status          = SPD_UDEV_READY;
    dev->udev->errcode         = 0;
    dev->udev->transfer_length = 0;
    init_waitqueue_head(&dev->udev->pollwq);
  }

  MAJOR_NR = SPD_UDEV_MAJOR;
  if(MAJOR_NR){
    id = MKDEV(MAJOR_NR, 0);
    retval = register_chrdev_region(id, SPD_N_DEV, DEVICE_NAME);
    if(unlikely(retval < 0)){
      PERROR("register_chrdev() failed(%d)", retval);
      return retval;
    }
  } else {
    retval = alloc_chrdev_region(&id, 0, SPD_N_DEV, DEVICE_NAME);
    if(unlikely(retval < 0)){
      PERROR("alloc_chrdev_region() failed(%d)", retval);
      return retval;
    }
    MAJOR_NR = MAJOR(id);
  }

  for(i = 0; i < SPD_N_DEV; i++){
    dev = &spd_dev[i];
    cdev_init(&dev->udev->cdev, &fops);
    dev->udev->cdev.owner = THIS_MODULE;
    id = MKDEV(MAJOR_NR, i);
    retval = cdev_add(&dev->udev->cdev, id, 1);
    if(unlikely(retval < 0)){
      PERROR("<spdu%c>cdev_add() failed(%d)", i+'a', retval);
    }
  }

  return 0;
}


int spd_udev_exit(void)
{
  int i;
  dev_t id;
  spd_dev_t *dev;
  PTRACE();

  for(i = 0; i < SPD_N_DEV; i++){
    dev = &spd_dev[i];
    cdev_del(&dev->udev->cdev);
  }
  id = MKDEV(MAJOR_NR, 0);
  unregister_chrdev_region(id, SPD_N_DEV);

  return 0;
}


static void udev_event_handler(spd_dev_t *dev, u32 event, unsigned long arg)
{
  PTRACE();
}


static int udev_open(struct inode *inode, struct file *file)
{
  spd_dev_t *dev;
  int id;
  PTRACE();

  id = iminor(inode);
  if(unlikely(id >= SPD_N_DEV)){
    PERROR("invalid minor number(%d)", id);
    return -ENODEV;
  }

  dev = &spd_dev[id];
  file->private_data = (void *)dev;

  return 0;
}


static int udev_release(struct inode *inode, struct file *filp)
{
  PTRACE();
  return 0;
}


static int udev_ioctl(struct inode *inode, struct file *file,
					  unsigned int cmd, unsigned long arg)
{
  int retval;
  spd_dev_t *dev = file->private_data;
  PTRACE();

  switch(cmd){
  case P2UIOC_CHECK_POWER_MODE:
    {
      retval = ioc_udev_check_power_mode(dev, arg);
      break;
    }

  case P2UIOC_IDENTIFY_DEVICE:
    {
      retval = ioc_udev_identify_device(dev, arg);
      break;
    }

  case P2UIOC_READ_MASTER_DMA:
    {
      retval = ioc_udev_read_sector(dev, arg);
      break;
    }

  case P2UIOC_WRITE_MASTER_DMA:
    {
      retval = ioc_udev_write_sector(dev, arg);
      break;
    }

  case P2UIOC_GET_STATUS:
    {
      retval = ioc_udev_get_status(dev, arg);
      break;
    }

  case P2UIOC_LOG_SENSE:
    {
      retval = ioc_udev_log_sense(dev, arg);
      break;
    }

  case P2UIOC_LOG_WRITE:
    {
      retval = ioc_udev_log_write(dev, arg);
      break;
    }

  case P2UIOC_BLK_ERASE:
    {
      retval = ioc_udev_block_erase(dev, arg);
      break;
    }

  case P2UIOC_SEC_ERASE:
    {
      retval = ioc_udev_sector_erase(dev, arg);
      break;
    }

  case P2UIOC_FW_UPDATE:
    {
      retval = ioc_udev_firm_update(dev, arg);
      break;
    }

  default:
    {
      PERROR("<spdu%c>unknown ioctl command(%d)", dev->id+'a', cmd);
      retval = -EINVAL;
    }
  }

  return retval;
}


static unsigned int udev_poll(struct file *file,
			      struct poll_table_struct *poll_table)
{
  spd_dev_t *dev;
  u32 events = 0;
  unsigned long flags = 0;
  PTRACE();

  dev = (spd_dev_t *)file->private_data;
  poll_wait(file, &dev->udev->pollwq, poll_table);

  spin_lock_irqsave(&dev->lock, flags);
  if(dev->udev->status == SPD_UDEV_ERROR){
    if(dev->udev->errcode == -ENODEV){
      events = POLLHUP;
    } else {
      events = POLLERR;
    }
  } else if(dev->udev->status == SPD_UDEV_DONE){
    events =  (POLLIN|POLLOUT);
  }
  spin_unlock_irqrestore(&dev->lock, flags);
  return events;
}


static inline u32 udev_get_sector(struct SET_ATA_REG *reg)
{
  return ( ((u32)(reg->HeadNum&0x0f)<<24)|
          ((u32)reg->CylHigh<<16)|
          ((u32)reg->CylLow<<8)|
          (u32)reg->SecNum );
}


static int ioc_udev_check_power_mode(spd_dev_t *dev, unsigned long arg)
{
  PTRACE();

  if(unlikely(!spd_is_IOE(dev))){
    return -EFAULT;
  }

  return 0;
}


static int ioc_udev_identify_device(spd_dev_t *dev, unsigned long arg)
{
  int retval;
  struct P2_SET_DATA parm;
  PTRACE();

  if(unlikely(!spd_is_IOE(dev))){
    return -ENODEV;
  }

  if(spd_io_trylock(dev)){
    return -EBUSY;
  }

  retval = copy_from_user(&parm, (void *)arg, sizeof(parm));
  if(unlikely(retval < 0)){
    PERROR("<spdu%c>copy_form_user() failed(%d)", dev->id+'a', retval);
    spd_io_unlock(dev);
    return -EFAULT;
  }

  dev->timeout = SPD_CMD_TIMEOUT;
  dev->retry   = SPD_NO_RETRY;
  retval = spd_identify_device(dev, (u16 *)dev->tmp_buf);
  if(unlikely(retval < 0)){
    PERROR("<spdu%c>spd_identify_device() failed(%d)", dev->id+'a', retval);
    spd_io_unlock(dev);
    return retval;
  }

  retval = copy_to_user((void *)parm.TransferAddress, 
                        dev->tmp_buf, SPD_HARDSECT_SIZE);
  if(unlikely(retval < 0)){
    PERROR("<spdu%c>copy_to_user() failed(%d)", dev->id+'a', retval);
  }
  spd_io_unlock(dev);
  return retval;
}


static void udev_complete_handler(spd_dev_t *dev)
{
  PTRACE();

  PINFO("<spdu%c>dma complete time=%dms, errcode=%d",
        dev->id+'a', jiffies_to_msecs(dev->ticks), dev->errcode);

  if(dev->errcode < 0){
    dev->udev->status  = SPD_UDEV_ERROR;
    dev->udev->errcode = dev->errcode;
    dev->udev->transfer_length = 0;
  } else {
    dev->udev->status  = SPD_UDEV_DONE;
    dev->udev->errcode = 0;
  }

  spd_io_unlock(dev);
  wake_up_interruptible(&dev->udev->pollwq);
}


static inline int udev_make_sg(spd_dev_t *dev,
                               u8 *virt_addr, u32 size, int io_dir)
{
  spd_scatterlist_t *sg = dev->sg;
  int n = 0;
  PTRACE();

  if(io_dir == SPD_DIR_READ){
    spd_dma_cache_inv(dev->dev, virt_addr, size);
  } else {
    spd_dma_cache_wback(dev->dev, virt_addr, size);
  }

  while(size){
    sg[n].bus_address = cpu_to_le32(virt_to_bus(virt_addr));

    if(size <= SPD_SG_MAX_COUNT){
      sg[n].count = cpu_to_le32(size);
      sg[n].count |= cpu_to_le32(SPD_SG_ENDMARK);
      n++;
      break;
    }
    sg[n].count = cpu_to_le32(SPD_SG_MAX_COUNT);
    virt_addr  += SPD_SG_MAX_COUNT;
    size       -= SPD_SG_MAX_COUNT;
    n++;

    if(unlikely(n >= SPD_SG_N_ENTRY)){
      PERROR("<spdu%c>scatterlist overflow(%d)", dev->id+'a', n);
      return -ENOMEM;
    }
  }
  spd_dma_cache_wback(dev->dev, sg, sizeof(spd_scatterlist_t)*n);
  dev->sg_n_entry = n;

  return 0;
}


static int ioc_udev_read_sector(spd_dev_t *dev, unsigned long arg)
{
  int retval;
  struct P2_SET_DATA parm;
  u16 count;
  u32 sector;
  PTRACE();

  if(unlikely(!spd_is_IOE(dev))){
    return -ENODEV;
  }

  if(spd_io_trylock(dev)){
    return -EBUSY;
  }

  retval = copy_from_user(&parm, (void *)arg, sizeof(parm));
  if(unlikely(retval < 0)){
    PERROR("<spdu%c>copy_form_user() failed(%d)", dev->id+'a', retval);
    dev->udev->status  = SPD_UDEV_ERROR;
    dev->udev->errcode = retval;
    spd_io_unlock(dev);
    return retval;
  }
  sector = udev_get_sector(&(parm.cbSetATAReg));
  count  = (((u16)parm.cbSetATAReg.Feature<<8)|(u16)parm.cbSetATAReg.SecCnt);

  retval = udev_make_sg(dev,
                        (u8 *)parm.TransferAddress,
                        count*SPD_HARDSECT_SIZE,
                        SPD_DIR_READ);

  if(unlikely(retval < 0)){ 
    PERROR("<spdu%c>udev_make_sg() failed(%d)", dev->id+'a', retval);
    dev->udev->status          = SPD_UDEV_ERROR;
    dev->udev->errcode         = retval;
    dev->udev->transfer_length = 0;
    spd_io_unlock(dev);
    return retval;
  }

  dev->udev->status          = SPD_UDEV_READ;
  dev->udev->errcode         = 0;
  dev->udev->transfer_length = count*SPD_HARDSECT_SIZE;
  dev->timeout               = dev->dma_timeout;
  dev->retry                 = SPD_NO_RETRY;
  dev->complete_handler      = udev_complete_handler;

  retval = spd_read_sector(dev, sector, count, dev->sg);
  if(unlikely(retval < 0)){
    dev->udev->status = SPD_UDEV_ERROR;
    dev->udev->errcode = retval;
    dev->udev->transfer_length = 0;
    PERROR("<spdu%c>spd_read_sector() failed(%d)", dev->id+'a', retval);
    spd_io_unlock(dev);
  }
  return retval;
}


static int ioc_udev_write_sector(spd_dev_t *dev, unsigned long arg)
{
  int retval;
  struct P2_SET_DATA parm;
  u16 count;
  u32 sector;
  PTRACE();

  if(unlikely(!spd_is_IOE(dev))){
    return -ENODEV;
  }

  if(spd_io_trylock(dev)){
    return -EBUSY;
  }

  retval = copy_from_user(&parm, (void *)arg, sizeof(parm));
  if(unlikely(retval < 0)){
    PERROR("<spdu%c>copy_form_user() failed(%d)", dev->id+'a', retval);
    dev->udev->status          = SPD_UDEV_ERROR;
    dev->udev->errcode         = retval;
    dev->udev->transfer_length = 0;
    spd_io_unlock(dev);
    return retval;
  }
  sector = udev_get_sector(&(parm.cbSetATAReg));
  count  = (((u16)parm.cbSetATAReg.Feature<<8)|(u16)parm.cbSetATAReg.SecCnt);

  retval = udev_make_sg(dev,
                        (u8 *)parm.TransferAddress,
                        count*SPD_HARDSECT_SIZE,
                        SPD_DIR_WRITE);

  if(unlikely(retval < 0)){
    PERROR("<spdu%c>udev_make_sg() failed(%d)", dev->id+'a', retval);
    dev->udev->status          = SPD_UDEV_ERROR;
    dev->udev->errcode         = retval;
    dev->udev->transfer_length = 0;
    spd_io_unlock(dev);
    return retval;
  }

  dev->udev->status          = SPD_UDEV_WRITE;
  dev->udev->errcode         = 0;
  dev->udev->transfer_length = count*SPD_HARDSECT_SIZE;
  dev->timeout               = dev->dma_timeout;
  dev->retry                 = SPD_NO_RETRY;
  dev->complete_handler      = udev_complete_handler;

  retval = spd_write_sector(dev, sector, count, dev->sg);
  if(unlikely(retval < 0)){
    dev->udev->status = SPD_UDEV_ERROR;
    dev->udev->errcode = retval;
    dev->udev->transfer_length = 0;
    PERROR("<spdu%c>spd_write_sector() failed(%d)", dev->id+'a', retval);
    spd_io_unlock(dev);
  }
  return retval;
}


static int ioc_udev_get_status(spd_dev_t *dev, unsigned long arg)
{
  int retval;
  unsigned long flags = 0;
  struct P2_GET_DATA parm;
  PTRACE();

  memset(&parm, 0, sizeof(parm));

  spin_lock_irqsave(&dev->lock, flags);
  switch(dev->udev->status){
  case SPD_UDEV_READY:
    {
      spin_unlock_irqrestore(&dev->lock, flags);
      return -EFAULT;
    }

  case SPD_UDEV_READ:
  case SPD_UDEV_WRITE:
    {
      spin_unlock_irqrestore(&dev->lock, flags);
      return -EBUSY;
    }

  case SPD_UDEV_ERROR:
    {
      dev->udev->status          = SPD_UDEV_READY;
      dev->udev->errcode         = 0;
      dev->udev->transfer_length = 0;
      spin_unlock_irqrestore(&dev->lock, flags);
      return -EIO;
    }

  case SPD_UDEV_DONE:
    {
      parm.TransferLength        = dev->udev->transfer_length;
      dev->udev->status          = SPD_UDEV_READY;
      dev->udev->errcode         = 0;
      dev->udev->transfer_length = 0;
      spin_unlock_irqrestore(&dev->lock, flags);
      retval = copy_to_user((void *)arg, &parm, sizeof(parm));
      if(unlikely(retval < 0)){
        PERROR("<spdu%c>copy_to_user() failed(%d)", dev->id+'a', retval);
      }
      return retval;
    }
  }
  spin_unlock_irqrestore(&dev->lock, flags);

  return -EFAULT;
}


static int ioc_udev_log_sense(spd_dev_t *dev, unsigned long arg)
{
  int retval;
  u8 page;
  struct P2_SET_DATA parm;
  PTRACE();

  if(unlikely(!spd_is_IOE(dev))){
    return -ENODEV;
  }

  if(spd_io_trylock(dev)){
    return -EBUSY;
  }

  retval = copy_from_user(&parm, (void *)arg, sizeof(parm));
  if(unlikely(retval < 0)){
    PERROR("<spdu%c>copy_form_user() failed(%d)", dev->id+'a', retval);
    spd_io_unlock(dev);
    return retval;
  }
  page = parm.cbSetATAReg.SecNum;

  dev->timeout = SPD_CMD_LONG_TIMEOUT;
  dev->retry   = SPD_NO_RETRY;
  retval = spd_log_sense(dev, page, (u8 *)dev->tmp_buf);
  if(unlikely(retval < 0)){
    PERROR("<spdu%c>spd_log_sense() failed(%d)", dev->id+'a', retval);
    spd_io_unlock(dev);
    return retval;
  }

  memcpy((void *)parm.TransferAddress, dev->tmp_buf, SPD_HARDSECT_SIZE);

  spd_io_unlock(dev);

  return 0;
}


static int ioc_udev_log_write(spd_dev_t *dev, unsigned long arg)
{
  int retval;
  u8 page;
  struct P2_SET_DATA parm;
  PTRACE();

  if(unlikely(!spd_is_IOE(dev))){
    return -ENODEV;
  }

  if(spd_io_trylock(dev)){
    return -EBUSY;
  }

  retval = copy_from_user(&parm, (void *)arg, sizeof(parm));
  if(unlikely(retval < 0)){
    PERROR("<spdu%c>copy_form_user() failed(%d)", dev->id+'a', retval);
    spd_io_unlock(dev);
    return retval;
  }
  page = parm.cbSetATAReg.SecNum;

  dev->timeout = SPD_CMD_LONG_TIMEOUT;
  dev->retry   = SPD_NO_RETRY;
  retval = spd_log_write(dev, page, (u8 *)parm.TransferAddress);
  if(unlikely(retval < 0)){
    PERROR("<spdu%c>spd_log_write() failed(%d)", dev->id+'a', retval);
  }
  spd_io_unlock(dev);
  return retval;
}


static int ioc_udev_block_erase(spd_dev_t *dev, unsigned long arg)
{
  int retval;
  u32 sector;
  u8 count;
  struct P2_SET_DATA parm;
  PTRACE();

  if(unlikely(!spd_is_IOE(dev))){
    return -ENODEV;
  }

  if(spd_io_trylock(dev)){
    return -EBUSY;
  }

  retval = copy_from_user(&parm, (void *)arg, sizeof(parm));
  if(unlikely(retval < 0)){
    PERROR("<spdu%c>copy_form_user() failed(%d)", dev->id+'a', retval);
    spd_io_unlock(dev);
    return retval;
  }
  sector = udev_get_sector(&(parm.cbSetATAReg));
  count  = parm.cbSetATAReg.SecCnt;

  dev->timeout = SPD_CMD_LONG_TIMEOUT;
  dev->retry   = SPD_NO_RETRY;
  retval = spd_block_erase(dev, sector, count);
  if(unlikely(retval < 0)){
    PERROR("<spdu%c>spd_block_erase() failed(%d)", dev->id+'a', retval);
  }
  spd_io_unlock(dev);
  return retval;
}


static int ioc_udev_sector_erase(spd_dev_t *dev, unsigned long arg)
{
  int retval;
  u32 sector;
  u8 count;
  struct P2_SET_DATA parm;
  PTRACE();

  if(unlikely(!spd_is_IOE(dev))){
    return -ENODEV;
  }
  
  if(spd_io_trylock(dev)){
    return -EBUSY;
  }

  retval = copy_from_user(&parm, (void *)arg, sizeof(parm));
  if(unlikely(retval < 0)){
    PERROR("<spdu%c>copy_form_user() failed(%d)", dev->id+'a', retval);
    spd_io_unlock(dev);
    return retval;
  }
  sector = udev_get_sector(&(parm.cbSetATAReg));
  count  = parm.cbSetATAReg.SecCnt;

  dev->timeout = SPD_CMD_LONG_TIMEOUT;
  dev->retry   = SPD_NO_RETRY;
  retval = spd_sector_erase(dev, sector, count);
  if(unlikely(retval < 0)){
    PERROR("<spdu%c>spd_sector_erase() failed(%d)", dev->id+'a', retval);
  }
  spd_io_unlock(dev);
  return retval;
}


static int ioc_udev_firm_update(spd_dev_t *dev, unsigned long arg)
{
  int retval;
  u8 count, asel;
  struct P2_SET_DATA parm;
  PTRACE();

  if(unlikely(!spd_is_IOE(dev))){
    return -ENODEV;
  }

  if(spd_io_trylock(dev)){
    return -EBUSY;
  }

  retval = copy_from_user(&parm, (void *)arg, sizeof(parm));
  if(unlikely(retval < 0)){
    PERROR("<spdu%c>copy_form_user() failed(%d)", dev->id+'a', retval);
    spd_io_unlock(dev);
    return retval;
  }

  count = parm.cbSetATAReg.SecCnt;
  asel  = parm.cbSetATAReg.HeadNum;
  dev->timeout = SPD_CMD_LONGLONG_TIMEOUT;
  dev->retry   = SPD_NO_RETRY;
  retval = spd_firm_update(dev, count, (u8 *)parm.TransferAddress, asel);
  if(unlikely(retval < 0)){
    PERROR("<spdu%c>spd_firm_update() failed(%d)", dev->id+'a', retval);
  }
  spd_io_unlock(dev);
  return retval;
}
