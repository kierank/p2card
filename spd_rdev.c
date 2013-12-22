/*
 P2card raw device modules
 $Id: spd_rdev.c 223 2006-09-19 11:03:44Z hiraoka $
 */

static int rdev_major;
#define MAJOR_NR           rdev_major
#define DEVICE_NR(device)  MINOR(device)
#define DEVICE_NAME        "spdr"

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
#include "spd_rdev.h"

#line __LINE__ "spd_rdev.c" /* Replace full path(__FILE__) to spd_rdev.c. */

static inline void rdev_event_card_ready(spd_dev_t *dev, unsigned long arg);
static inline void rdev_event_card_remove(spd_dev_t *dev, unsigned long arg);
static inline void rdev_event_disk_enable(spd_dev_t *dev, unsigned long arg);
static inline void rdev_event_card_error(spd_dev_t *dev,  int errcode);
static inline void rdev_event_ricoh_error(spd_dev_t *dev, int io_dir);
static inline void rdev_event_fatal_error(spd_dev_t *dev, int errcode);
static inline void rdev_event_disk_open(spd_dev_t *dev, int usage);
static inline void rdev_event_disk_close(spd_dev_t *dev, int usage);
static void rdev_event_handler(spd_dev_t *dev, u32 event, unsigned long arg);

static int  rdev_open(struct inode *inode, struct file *file);
static int  rdev_release(struct inode *inode, struct file *file);
static int  rdev_ioctl(struct inode *inode, struct file *file,
		       unsigned int cmd, unsigned long arg);
static unsigned int rdev_poll(struct file *file,
			      struct poll_table_struct *poll_table);
static void rdev_complete_handler(spd_dev_t *dev);
static int  ioc_rdev_get_card_status(spd_dev_t *dev, unsigned long arg);
static int  ioc_rdev_check_card_status(spd_dev_t *dev, unsigned long arg);
static int  ioc_rdev_awake_card_status(spd_dev_t *dev, unsigned long arg);
static int  ioc_rdev_clear_card_status(spd_dev_t *dev, unsigned long arg);
static int  ioc_rdev_log_sense(spd_dev_t *dev, unsigned long arg);
static int  ioc_rdev_get_card_id(spd_dev_t *dev, unsigned long arg);
static int  ioc_rdev_check_write_protect(spd_dev_t *dev, unsigned long arg);
static int  ioc_rdev_get_dma_status(spd_dev_t *dev, unsigned long arg);
static int  ioc_rdev_set_dma_status(spd_dev_t *dev, unsigned long arg);
static int  ioc_rdev_check_reading(spd_dev_t *dev, unsigned long arg);
static int  ioc_rdev_get_err_status(spd_dev_t *dev, unsigned long arg);
static int  ioc_rdev_set_err_status(spd_dev_t *dev, unsigned long arg);
static int  ioc_rdev_get_errno(spd_dev_t *dev, unsigned long arg);
static int  ioc_rdev_preprocess_io_retry(spd_dev_t *dev, unsigned long arg);
static int  ioc_rdev_identify_device(spd_dev_t *dev, unsigned long arg);
static int  ioc_rdev_card_terminate(spd_dev_t *dev, unsigned long arg);
static int  ioc_rdev_direct_read(spd_dev_t *dev, unsigned long arg);
static int  ioc_rdev_direct_write(spd_dev_t *dev, unsigned long arg);
static int  ioc_rdev_direct_seq_write(spd_dev_t *dev, unsigned long arg);
static int  ioc_rdev_direct_rmw(spd_dev_t *dev, unsigned long arg);
static int  ioc_rdev_get_card_params(spd_dev_t *dev, unsigned long arg);
static int  ioc_rdev_set_card_params(spd_dev_t *dev, unsigned long arg);
static int  ioc_rdev_card_initialize(spd_dev_t *dev, unsigned long arg);
static int  ioc_rdev_card_rescue(spd_dev_t *dev, unsigned long arg);

static int  ioc_rdev_secured_rec(spd_dev_t *dev, unsigned long arg);
static int  ioc_rdev_start_rec(spd_dev_t *dev, unsigned long arg);
static int  ioc_rdev_end_rec(spd_dev_t *dev, unsigned long arg);
static int  ioc_rdev_set_new_au(spd_dev_t *dev, unsigned long arg);
static int  ioc_rdev_fsmi_write(spd_dev_t *dev, unsigned long arg);
static int  ioc_rdev_sdcmd_write(spd_dev_t *dev, unsigned long arg);
static int  ioc_rdev_sd_device_reset(spd_dev_t *dev);
static int  ioc_rdev_go_hibernate(spd_dev_t *dev);
static int  ioc_rdev_table_recover(spd_dev_t *dev, unsigned long arg);
static int  ioc_rdev_au_erase(spd_dev_t *dev, unsigned long arg);

static int  ioc_rdev_lmg(spd_dev_t *dev, unsigned long arg);
static int  ioc_rdev_get_sst(spd_dev_t *dev, unsigned long arg);
static int  ioc_rdev_get_dinfo(spd_dev_t *dev, unsigned long arg);
static int  ioc_rdev_dmg(spd_dev_t *dev, unsigned long arg);
static int  ioc_rdev_esw(spd_dev_t *dev, unsigned long arg);
static int  ioc_rdev_get_ph(spd_dev_t *dev, unsigned long arg);
static int  ioc_rdev_get_linfo(spd_dev_t *dev, unsigned long arg);
static int  ioc_rdev_dau(spd_dev_t *dev, unsigned long arg);
static int  ioc_rdev_set_dparam(spd_dev_t *dev, unsigned long arg);
static int  ioc_rdev_get_phs(spd_dev_t *dev, unsigned long arg);
static int  ioc_rdev_set_aut_status(spd_dev_t *dev, unsigned long arg);


static spd_rdev_t spd_rdev[SPD_N_DEV];
static struct file_operations fops = {
  .owner   = THIS_MODULE,
  .open    = rdev_open,
  .release = rdev_release,
  .ioctl   = rdev_ioctl,
  .poll    = rdev_poll,
};


static wait_queue_head_t rdev_status_wq;
static spinlock_t rdev_status_lock;
static struct p2_card_status rdev_card_status;
static struct p2_err_status  rdev_err_status;
static struct p2_dma_status  rdev_dma_status;


int spd_rdev_init(void)
{
  int i;
  int retval;
  dev_t id;
  spd_dev_t *dev;
  PTRACE();

  memset(spd_rdev,          0, sizeof(spd_rdev));
  memset(&rdev_card_status, 0, sizeof(rdev_card_status));
  memset(&rdev_err_status,  0, sizeof(rdev_err_status));
  memset(&rdev_dma_status,  0, sizeof(rdev_dma_status));
  init_waitqueue_head(&rdev_status_wq);
  spin_lock_init(&rdev_status_lock);

  for(i = 0; i < SPD_N_DEV; i++){
    dev = &spd_dev[i];
    dev->rdev = &spd_rdev[i];
    dev->rdev->events = 0;
    dev->rdev->event_mask = (SPD_EVENT_CARD_READY|
			     SPD_EVENT_CARD_REMOVE|
			     SPD_EVENT_CARD_ERROR|
			     SPD_EVENT_RICOH_ERROR|
                             SPD_EVENT_DISK_ENABLE|
                             SPD_EVENT_DISK_OPEN|
                             SPD_EVENT_DISK_CLOSE|
                             SPD_EVENT_DIRECT_COMPLETE|
                             SPD_EVENT_FATAL_ERROR);

    dev->rdev->event_handler = rdev_event_handler;
    init_completion(&dev->rdev->dma);
    init_waitqueue_head(&dev->rdev->pollwq);
  }

  MAJOR_NR = SPD_RDEV_MAJOR;
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
    cdev_init(&dev->rdev->cdev, &fops);
    dev->rdev->cdev.owner = THIS_MODULE;
    id = MKDEV(MAJOR_NR, i);
    retval = cdev_add(&dev->rdev->cdev, id, 1);
    if(unlikely(retval < 0)){
      PERROR("<spdr%c>cdev_add() failed(%d)", i+'a', retval);
    }
  }

  return 0;
}


int spd_rdev_exit(void)
{
  int i;
  dev_t id;
  spd_dev_t *dev;
  PTRACE();

  for(i = 0; i < SPD_N_DEV; i++){
    dev = &spd_dev[i];
    cdev_del(&dev->rdev->cdev);
  }
  id = MKDEV(MAJOR_NR, 0);
  unregister_chrdev_region(id, SPD_N_DEV);

  return 0;
}


static inline void rdev_event_card_ready(spd_dev_t *dev, unsigned long arg)
{
  unsigned long flags = 0;
  p2_slot_image mask = 1<<dev->id;
  PTRACE();

  spin_lock_irqsave(&rdev_status_lock, flags);
  dev->rdev->events |= POLLIN;

  rdev_card_status.slot_image |= mask;
  rdev_card_status.open_request &= ~mask;
  spin_unlock_irqrestore(&rdev_status_lock, flags);

  wake_up_interruptible(&rdev_status_wq);
  wake_up_interruptible(&dev->rdev->pollwq);
}


static inline void rdev_event_card_remove(spd_dev_t *dev, unsigned long arg)
{
  unsigned long flags = 0;
  p2_slot_image mask = 1<<dev->id;
  PTRACE();

  spin_lock_irqsave(&rdev_status_lock, flags);
  spd_clr_status(dev, SPD_CARD_ERROR);
  dev->rdev->events |= POLLIN;

  rdev_card_status.slot_image   &= ~mask;
  rdev_card_status.open_request &= ~mask;
  if((rdev_card_status.open_image & mask) || spd_is_AUE(dev)){
    rdev_card_status.release_request |= mask;
  }
  rdev_err_status.carderr &= ~mask;
  spd_clr_AUE(dev);
  spin_unlock_irqrestore(&rdev_status_lock, flags);

  wake_up_interruptible(&rdev_status_wq);
  wake_up_interruptible(&dev->rdev->pollwq);
}


static inline void rdev_event_card_error(spd_dev_t *dev, int errcode)
{
  unsigned long flags = 0;
  p2_slot_image mask = 1<<dev->id;
  PTRACE();

  spin_lock_irqsave(&rdev_status_lock, flags);
  spd_set_status(dev, SPD_CARD_ERROR);
  dev->rdev->errcode = errcode;
  dev->rdev->events |= POLLERR;

  rdev_err_status.carderr |= mask;
  spin_unlock_irqrestore(&rdev_status_lock, flags);

  wake_up_interruptible(&dev->rdev->pollwq);
}


static inline void rdev_event_ricoh_error(spd_dev_t *dev, int io_dir)
{
  unsigned long flags = 0;
  p2_slot_image mask = 1<<dev->id;
  PTRACE();

  spin_lock_irqsave(&rdev_status_lock, flags);
  spd_set_status(dev, SPD_RICOH_ERROR);
  dev->rdev->errcode = -EIO;

  if(io_dir == SPD_DIR_READ){
    PERROR("<spd%c>R5C812 on READ", dev->id+'a');
    rdev_dma_status.read |= mask;
  } else if(io_dir == SPD_DIR_WRITE){
    PERROR("<spd%c>R5C812 on WRITE", dev->id+'a');
    rdev_dma_status.write |= mask;
  } else {
    PERROR("<spd%c>R5C812 on Unknown", dev->id+'a');
    rdev_dma_status.read  |= mask;
    rdev_dma_status.write |= mask;
  }

  spin_unlock_irqrestore(&rdev_status_lock, flags);
}


static inline void rdev_event_disk_enable(spd_dev_t *dev, unsigned long arg)
{
  unsigned long flags = 0;
  p2_slot_image mask = 1<<dev->id;
  PTRACE();

  spin_lock_irqsave(&rdev_status_lock, flags);
  rdev_card_status.open_request |= mask;
  spin_unlock_irqrestore(&rdev_status_lock, flags);

  wake_up_interruptible(&rdev_status_wq);
}


static inline void rdev_event_disk_open(spd_dev_t *dev, int usage)
{
  unsigned long flags = 0;
  p2_slot_image mask = 1<<dev->id;
  PTRACE();

  if(usage > 1){
    return;
  }

  spin_lock_irqsave(&rdev_status_lock, flags);
  if((~rdev_card_status.release_request&mask) &&
     (rdev_card_status.slot_image&mask)){
    rdev_card_status.open_request &= ~mask;
    rdev_card_status.open_image |= mask;
  }
  spin_unlock_irqrestore(&rdev_status_lock, flags);

  wake_up_interruptible(&rdev_status_wq);
}


static inline void rdev_event_disk_close(spd_dev_t *dev, int usage)
{
  unsigned long flags = 0;
  p2_slot_image mask = 1<<dev->id;
  PTRACE();

  if(usage > 0){
    return;
  }

  spin_lock_irqsave(&rdev_status_lock, flags);
  rdev_card_status.open_image &= ~mask;
  rdev_card_status.release_request &= ~mask;
  spin_unlock_irqrestore(&rdev_status_lock, flags);

  wake_up_interruptible(&rdev_status_wq);
}


static inline void rdev_event_fatal_error(spd_dev_t *dev, int errcode)
{
  unsigned long flags = 0;
  PTRACE();

  spin_lock_irqsave(&rdev_status_lock, flags);
  spd_set_status(dev, SPD_CARD_ERROR);
  dev->rdev->errcode = errcode;
  dev->rdev->events |= POLLERR;
  spin_unlock_irqrestore(&rdev_status_lock, flags);

  wake_up_interruptible(&dev->rdev->pollwq);
}


static inline void rdev_event_direct_complete(spd_dev_t *dev, int errcode)
{
  unsigned long flags = 0;
  PTRACE();

  if(errcode == 0){
    spin_lock_irqsave(&rdev_status_lock, flags);
    dev->rdev->errcode = 0;
    dev->rdev->events |= POLLOUT;
    spin_unlock_irqrestore(&rdev_status_lock, flags);
    wake_up_interruptible(&dev->rdev->pollwq);
    return;
  }

  spin_lock_irqsave(&rdev_status_lock, flags);
  dev->rdev->errcode = errcode;
  dev->rdev->events |= (POLLERR|POLLOUT);
  if(errcode == -ENODEV){ /* device lost */
    dev->rdev->events |= POLLIN;
  }
  spin_unlock_irqrestore(&rdev_status_lock, flags);
  wake_up_interruptible(&dev->rdev->pollwq);
}


static void rdev_event_handler(spd_dev_t *dev, u32 event, unsigned long arg)
{
  PTRACE();

  switch(event){
  case SPD_EVENT_CARD_READY:
    {
      if(!spd_is_DEB(dev)){
	rdev_event_card_ready(dev, arg);
      }
      break;
    }

  case SPD_EVENT_CARD_REMOVE:
    {
      rdev_event_card_remove(dev, arg);
      break;
    }

  case SPD_EVENT_DISK_ENABLE:
    {
      rdev_event_disk_enable(dev, arg);
      break;
    }

  case SPD_EVENT_DISK_OPEN:
    {
      rdev_event_disk_open(dev, (int)arg);
      break;
    }

  case SPD_EVENT_DISK_CLOSE:
    {
      rdev_event_disk_close(dev, (int)arg);
      break;
    }

  case SPD_EVENT_CARD_ERROR:
    {
      rdev_event_card_error(dev, (int)arg);
      break;
    }

  case SPD_EVENT_RICOH_ERROR:
    {
      rdev_event_ricoh_error(dev, (int)arg);
      break;
    }

  case SPD_EVENT_FATAL_ERROR:
    {
      rdev_event_fatal_error(dev,(int)arg);
      break;
    }

  case SPD_EVENT_DIRECT_COMPLETE:
    {
      rdev_event_direct_complete(dev,(int)arg);
      break;
    }
  }
}


static int rdev_open(struct inode *inode, struct file *file)
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


static int rdev_release(struct inode *inode, struct file *filp)
{
  PTRACE();
  return 0;
}


static int rdev_ioctl(struct inode *inode, struct file *file,
		      unsigned int cmd, unsigned long arg)
{
  int retval;
  spd_dev_t *dev = file->private_data;
  PTRACE();

  switch(cmd){
  case P2_GET_CARD_STATUS:
    {
      retval = ioc_rdev_get_card_status(dev, arg);
      break;
    }

  case P2_CHECK_CARD_STATUS:
    {
      retval = ioc_rdev_check_card_status(dev, arg);
      break;
    }

  case P2_AWAKE_CARD_STATUS:
    {
      retval = ioc_rdev_awake_card_status(dev, arg);
      break;
    }

  case P2_CLEAR_CARD_STATUS:
    {
      retval = ioc_rdev_clear_card_status(dev, arg);
      break;
    }

  case P2_COMMAND_LOG_SENSE:
    {
      retval = ioc_rdev_log_sense(dev, arg);
      break;
    }

  case P2_GET_CARD_ID:
    {
      retval = ioc_rdev_get_card_id(dev, arg);
      break;
    }

  case P2_CHECK_WRITE_PROTECT:
    {
      retval = ioc_rdev_check_write_protect(dev, arg);
      break;
    }

  case P2_GET_DMA_STATUS:
    {
      retval = ioc_rdev_get_dma_status(dev, arg);
      break;
    }

  case P2_SET_DMA_STATUS:
    {
      retval = ioc_rdev_set_dma_status(dev, arg);
      break;
    }

  case P2_CHECK_READING:
    {
      retval = ioc_rdev_check_reading(dev, arg);
      break;
    }

  case P2_GET_ERR_STATUS:
    {
      retval = ioc_rdev_get_err_status(dev, arg);
      break;
    }

  case P2_SET_ERR_STATUS:
    {
      retval = ioc_rdev_set_err_status(dev, arg);
      break;
    }

  case P2_GET_ERRNO:
    {
      retval = ioc_rdev_get_errno(dev, arg);
      break;
    }

  case P2_PREPROCESS_IO_RETRY:
    {
      retval = ioc_rdev_preprocess_io_retry(dev, arg);
      break;
    }

  case P2_COMMAND_IDENTIFY_DEVICE:
    {
      retval = ioc_rdev_identify_device(dev, arg);
      break;
    }

  case P2_TERMINATE:
    {
      retval = ioc_rdev_card_terminate(dev, arg);
      break;
    }

  case P2_DIRECT_READ:
    {
      retval = ioc_rdev_direct_read(dev, arg);
      break;
    }

  case P2_DIRECT_WRITE:
    {
      retval = ioc_rdev_direct_write(dev, arg);
      break;
    }

  case P2_DIRECT_SEQ_WRITE:
    {
      retval = ioc_rdev_direct_seq_write(dev, arg);
      break;
    }

  case P2_DIRECT_RMW:
    {
      retval = ioc_rdev_direct_rmw(dev, arg);
      break;
    }

  case P2_GET_CARD_PARAMS:
    {
      retval = ioc_rdev_get_card_params(dev, arg);
      break;
    }

  case P2_SET_CARD_PARAMS:
    {
      retval = ioc_rdev_set_card_params(dev, arg);
      break;
    }

  case P2_COMMAND_CARD_INITIALIZE:
    {
      retval = ioc_rdev_card_initialize(dev, arg);
      break;
    }

  case P2_COMMAND_CARD_RESCUE:
    {
      retval = ioc_rdev_card_rescue(dev, arg);
      break;
    }

  case P2_COMMAND_SECURED_REC:
    {
      retval = ioc_rdev_secured_rec(dev, arg);
      break;
    }

  case P2_COMMAND_START_REC:
    {
      retval = ioc_rdev_start_rec(dev, arg);
      break;
    }

  case P2_COMMAND_END_REC:
    {
      retval = ioc_rdev_end_rec(dev, arg);
      break;
    }

  case P2_COMMAND_SET_NEW_AU:
    {
      retval = ioc_rdev_set_new_au(dev, arg);
      break;
    }

  case P2_FSMI_WRITE:
    {
      retval = ioc_rdev_fsmi_write(dev, arg);
      break;
    }

  case P2_SDCMD_WRITE:
    {
      retval = ioc_rdev_sdcmd_write(dev, arg);
      break;
    }

  case P2_COMMAND_SD_DEVICE_RESET:
    {
      retval = ioc_rdev_sd_device_reset(dev);
      break;
    }

  case P2_COMMAND_GO_HIBERNATE:
    {
      retval = ioc_rdev_go_hibernate(dev);
      break;
    }

  case P2_COMMAND_TABLE_RECOVER:
    {
      retval = ioc_rdev_table_recover(dev, arg);
      break;
    }

  case P2_COMMAND_AU_ERASE:
    {
      retval = ioc_rdev_au_erase(dev, arg);
      break;
    }

  case P2_COMMAND_LMG:
    {
      retval = ioc_rdev_lmg(dev, arg);
      break;
    }

  case P2_COMMAND_GET_SST:
    {
      retval = ioc_rdev_get_sst(dev, arg);
      break;
    }

  case P2_COMMAND_GET_DINFO:
    {
      retval = ioc_rdev_get_dinfo(dev, arg);
      break;
    }

  case P2_COMMAND_DMG:
    {
      retval = ioc_rdev_dmg(dev, arg);
      break;
    }

  case P2_COMMAND_ESW:
    {
      retval = ioc_rdev_esw(dev, arg);
      break;
    }

  case P2_COMMAND_GET_PH:
    {
      retval = ioc_rdev_get_ph(dev, arg);
      break;
    }

  case P2_COMMAND_GET_LINFO:
    {
      retval = ioc_rdev_get_linfo(dev, arg);
      break;
    }

  case P2_COMMAND_DAU:
    {
      retval = ioc_rdev_dau(dev, arg);
      break;
    }

  case P2_COMMAND_SET_DPARAM:
    {
      retval = ioc_rdev_set_dparam(dev, arg);
      break;
    }

  case P2_COMMAND_GET_PHS:
    {
      retval = ioc_rdev_get_phs(dev, arg);
      break;
    }

  case P2_SET_AUT_STATUS:
    {
      retval = ioc_rdev_set_aut_status(dev, arg);
      break;
    }

  default:
    {
      PERROR("<spdr%c>unknown ioctl command(%x)", dev->id+'a', cmd);
      retval = -EINVAL;
    }
  }

  return retval;
}


static unsigned int rdev_poll(struct file *file,
			      struct poll_table_struct *poll_table)
{
  spd_dev_t *dev;
  unsigned long flags = 0;
  u32 events = 0;
  PTRACE();

  dev = (spd_dev_t *)file->private_data;
  poll_wait(file, &dev->rdev->pollwq, poll_table);
  spin_lock_irqsave(&rdev_status_lock, flags);
  events = dev->rdev->events;
  dev->rdev->events = 0;
  spin_unlock_irqrestore(&rdev_status_lock, flags);

  return events;
}


static void rdev_complete_handler(spd_dev_t *dev)
{
  PTRACE();

  PINFO("<spdr%c>dma complete time=%dms, errcode=%d",
        dev->id+'a', jiffies_to_msecs(dev->ticks), dev->errcode);

  complete(&dev->rdev->dma);
}


static int ioc_rdev_get_card_status(spd_dev_t *dev, unsigned long arg)
{
  int retval;
  unsigned long flags = 0;
  PTRACE();

  spin_lock_irqsave(&rdev_status_lock, flags);
  retval = copy_to_user((void *)arg,
                        &rdev_card_status, sizeof(rdev_card_status));
  spin_unlock_irqrestore(&rdev_status_lock, flags);

  if(unlikely(retval < 0)){
    PERROR("<spdr%c>copy_to_user() failed(%d)", dev->id+'a', retval);
  }
  return retval;
}


static int ioc_rdev_check_card_status(spd_dev_t *dev, unsigned long arg)
{
  int retval;
  unsigned long flags = 0;
  struct p2_card_status last;
  wait_queue_t wait;
  PTRACE();

  retval = copy_from_user(&last, (void *)arg, sizeof(last));
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>copy_from_user() failed(%d)", dev->id+'a', retval);
    return retval;
  }

  spin_lock_irqsave(&rdev_status_lock, flags);
  retval = memcmp(&last, &rdev_card_status, sizeof(last));
  if(retval == 0) {
    init_waitqueue_entry(&wait, current);
    add_wait_queue(&rdev_status_wq, &wait);
    set_current_state(TASK_INTERRUPTIBLE);
    spin_unlock_irqrestore(&rdev_status_lock, flags);
    schedule();
    spin_lock_irqsave(&rdev_status_lock, flags);
    remove_wait_queue(&rdev_status_wq, &wait);
  }
  retval = copy_to_user((void *)arg,
                        &rdev_card_status, sizeof(rdev_card_status));
  spin_unlock_irqrestore(&rdev_status_lock, flags);

  if(unlikely(retval < 0)){
    PERROR("<spdr%c>copy_to_user() failed(%d)", dev->id+'a', retval);
  }
  return retval;
}


static int ioc_rdev_awake_card_status(spd_dev_t *dev, unsigned long arg)
{
  PTRACE();
  wake_up_interruptible(&rdev_status_wq);
  return 0;
}


static int ioc_rdev_clear_card_status(spd_dev_t *dev, unsigned long arg)
{
  unsigned long flags = 0;
  p2_slot_image mask = 1<<dev->id;
  PTRACE();

  spin_lock_irqsave(&rdev_status_lock, flags);
  rdev_card_status.release_request &= ~mask;
  spin_unlock_irqrestore(&rdev_status_lock, flags);

  return 0;
}


static int ioc_rdev_log_sense(spd_dev_t *dev, unsigned long arg)
{
  int retval;
  struct p2_log_sense_arg parm;
  PTRACE();

  if(unlikely(!spd_is_IOE(dev))){
    return -ENODEV;
  }

  retval = copy_from_user(&parm, (void *)arg, sizeof(parm));
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>copy_from_user() failed(%d)", dev->id+'a', retval);
    return retval;
  }

  if(spd_io_trylock(dev)){
    return -EBUSY;
  }

  dev->timeout = SPD_CMD_LONG_TIMEOUT; /* ! */
  dev->retry   = SPD_NO_RETRY;
  retval = spd_log_sense(dev, parm.page, (u8 *)dev->tmp_buf);
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>spd_log_sense() failed(%d)", dev->id+'a', retval);
    spd_io_unlock(dev);
    return retval;
  }

  retval = copy_to_user((void *)parm.buffer, dev->tmp_buf, SPD_HARDSECT_SIZE);
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>copy_to_user() failed(%d)", dev->id+'a', retval);
  }
  spd_io_unlock(dev);
  return retval;
}


static int ioc_rdev_get_card_id(spd_dev_t *dev, unsigned long arg)
{
  int retval;
  u8 *b;
  PTRACE();

  if(unlikely(!spd_is_IOE(dev))){
    return -ENODEV;
  }

  if(spd_io_trylock(dev)){
    return -EBUSY;
  }

  b = dev->tmp_buf;
  dev->timeout = SPD_CMD_TIMEOUT;
  dev->retry   = SPD_NO_RETRY;
  retval = spd_log_sense(dev, 0x20, b);
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>spd_log_sense() failed(%d)", dev->id+'a', retval);
    spd_io_unlock(dev);
    return retval;
  }
  retval = copy_to_user((void *)arg, b+24, SPD_CARD_ID_SIZE);
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>copy_to_user() failed(%d)", dev->id+'a', retval);
  }
  spd_io_unlock(dev);
  return retval;
}


static int ioc_rdev_check_write_protect(spd_dev_t *dev, unsigned long arg)
{
  int retval;
  PTRACE();

  if(unlikely(!spd_is_IOE(dev))){
    return -ENODEV;
  }

  if(spd_is_status(dev, SPD_WRITE_GUARD)){
    return 1;
  }

  if(spd_io_trylock(dev)){
    return -EBUSY;
  }
  dev->timeout = SPD_CMD_TIMEOUT;
  dev->retry   = SPD_NO_RETRY;
  retval = spd_check_protect(dev);
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>spd_check_protect() failed(%d)", dev->id+'a', retval);
  }
  spd_io_unlock(dev);
  return retval;
}


static int ioc_rdev_get_dma_status(spd_dev_t *dev, unsigned long arg)
{
  unsigned long flags = 0;
  int retval;
  PTRACE();

  spin_lock_irqsave(&rdev_status_lock, flags);
  retval = copy_to_user((void *)arg,
                        &rdev_dma_status,
                        sizeof(rdev_dma_status));
  spin_unlock_irqrestore(&rdev_status_lock, flags);

  if(unlikely(retval < 0)){
    PERROR("<spdr%c>copy_to_user() failed(%d)", dev->id+'a', retval);
  }
  return retval;
}


static int ioc_rdev_set_dma_status(spd_dev_t *dev, unsigned long arg)
{
  unsigned long flags = 0;
  int retval;
  PTRACE();

  spin_lock_irqsave(&rdev_status_lock, flags);
  retval = copy_from_user(&rdev_dma_status,
                          (void *)arg,
                          sizeof(rdev_dma_status));
  spin_unlock_irqrestore(&rdev_status_lock, flags);

  if(unlikely(retval < 0)){
    PERROR("<spdr%c>copy_from_user() failed(%d)", dev->id+'a', retval);
  }
  return retval;
}


static int ioc_rdev_check_reading(spd_dev_t *dev, unsigned long arg)
{
  int retval;
  spd_scatterlist_t *sg = dev->sg;
  PTRACE();

  if(unlikely(!spd_is_IOE(dev))){
    return -ENODEV;
  }

  if(spd_io_trylock(dev)){
    return -EBUSY;
  }
  
  sg[0].bus_address = cpu_to_le32(virt_to_bus(dev->tmp_buf));
  sg[0].count       = cpu_to_le32((SPD_HARDSECT_SIZE|SPD_SG_ENDMARK));
  dev->sg_n_entry = 1;
  spd_dma_cache_wback(dev->dev, sg, sizeof(spd_scatterlist_t)*1);

  dev->timeout = dev->dma_timeout;
  dev->retry   = SPD_NO_RETRY;
  dev->complete_handler = rdev_complete_handler;
  retval = spd_read_sector(dev, 0, 1, sg);
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>spd_read_sector() failed(%d)", dev->id+'a', retval);
    spd_io_unlock(dev);
    return retval;
  }
  wait_for_completion(&dev->rdev->dma);

  retval = dev->errcode;
  spd_io_unlock(dev);

  return retval;
}


static int ioc_rdev_get_err_status(spd_dev_t *dev, unsigned long arg)
{
  unsigned long flags = 0;
  int retval;
  PTRACE();

  spin_lock_irqsave(&rdev_status_lock, flags);
  retval = copy_to_user((void *)arg,
                        &rdev_err_status,
                        sizeof(rdev_err_status));
  spin_unlock_irqrestore(&rdev_status_lock, flags);

  if(unlikely(retval < 0)){
    PERROR("<spdr%c>copy_to_user() failed(%d)", dev->id+'a', retval);
  }

  return retval;
}


static int ioc_rdev_set_err_status(spd_dev_t *dev, unsigned long arg)
{
  unsigned long flags = 0;
  int retval;
  PTRACE();

  spin_lock_irqsave(&rdev_status_lock, flags);
  retval = copy_from_user(&rdev_err_status,
                          (void *)arg,
                          sizeof(rdev_err_status));
  spin_unlock_irqrestore(&rdev_status_lock, flags);

  if(unlikely(retval < 0)){
    PERROR("<spdr%c>copy_from_user() failed(%d)", dev->id+'a', retval);
  }

  return retval;
}


static int ioc_rdev_get_errno(spd_dev_t *dev, unsigned long arg)
{
  struct p2_errno parm;
  unsigned long flags = 0;
  int retval;
  PTRACE();

  memset(&parm, 0, sizeof(parm));

  spin_lock_irqsave(&rdev_status_lock, flags);
  parm.no = dev->rdev->errcode;
  dev->rdev->errcode = 0;
  spin_unlock_irqrestore(&rdev_status_lock, flags);

  retval = copy_to_user((void *)arg, &parm, sizeof(parm));
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>copy_to_user() failed(%d)", dev->id+'a', retval);
  }

  return retval;
}


static int ioc_rdev_identify_device(spd_dev_t *dev, unsigned long arg)
{
  int retval;
  PTRACE();

  if(unlikely(!spd_is_IOE(dev))){
    return -ENODEV;
  }

  if(spd_io_trylock(dev)){
    return -EBUSY;
  }
    
  dev->timeout = SPD_CMD_TIMEOUT;
  dev->retry   = SPD_NO_RETRY;
  retval = spd_identify_device(dev, (u16 *)dev->tmp_buf);
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>spd_identify_device() failed(%d)", dev->id+'a', retval);
    spd_io_unlock(dev);
    return retval;
  }

  retval = copy_to_user((void *)arg, dev->tmp_buf, SPD_HARDSECT_SIZE);
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>copy_to_user() failed(%d)", dev->id+'a', retval);
  }
  spd_io_unlock(dev);
  return retval;
}


static int ioc_rdev_card_terminate(spd_dev_t *dev, unsigned long arg)
{
  int retval;
  PTRACE();

  if(unlikely(!spd_is_IOE(dev))){
    return -ENODEV;
  }

  if(spd_io_trylock(dev)){
    return -EBUSY;
  }
  
  dev->timeout = SPD_CMD_TIMEOUT;
  dev->retry   = SPD_NO_RETRY;
  retval = spd_card_terminate(dev);
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>spd_card_terminate() failed(%d)", dev->id+'a', retval);
  }
  spd_io_unlock(dev);
  return retval;
}


static void rdev_direct_complete_handler(spd_dev_t *dev)
{
  PTRACE();

  PINFO("<spdr%c>complete time=%dms", dev->id+'a', jiffies_to_msecs(dev->ticks));
  spd_send_event(dev, SPD_EVENT_DIRECT_COMPLETE, dev->errcode);
  spd_io_unlock(dev);
}


static int ioc_rdev_direct_read(spd_dev_t *dev, unsigned long arg)
{
  int i;
  int retval;
  struct p2_direct_arg parm;
  int major = dev->spec_version >> 4;
  PTRACE();

  if(unlikely(!spd_is_IOE(dev))){
    retval = -ENODEV;
    goto ABORT;
  }
  
  retval = copy_from_user(&parm, (void *)arg, sizeof(parm));
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>copy_from_user() failed(%d)", dev->id+'a', retval);
    goto ABORT;
  }

  if(parm.sg_table_size > SPD_SG_SIZE){
    PERROR("<spdr%c>invalid sg table size(%x)", dev->id+'a', parm.sg_table_size);
    retval = -EINVAL;
    goto ABORT;
  }

  if(spd_io_trylock(dev)){
    retval = -EBUSY;
    goto ABORT;
  }

  retval  = copy_from_user(dev->sg, parm.sg_table, parm.sg_table_size);
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>copy_from_user() failed(%d)", dev->id+'a', retval);
    spd_io_unlock(dev);
    goto ABORT;
  }

  spd_dma_cache_wback(dev->dev, dev->sg, parm.sg_table_size);
  dev->sg_n_entry = parm.sg_table_size/sizeof(spd_scatterlist_t);
  for(i = 0; i < dev->sg_n_entry; i++){
    spd_dma_cache_inv(dev->dev,
		      bus_to_virt(le32_to_cpu((u32)dev->sg[i].bus_address)),
		      le32_to_cpu(dev->sg[i].count)&(~SPD_SG_ENDMARK));
  }

  dev->retry            = (major == 4) ? SPD_NO_RETRY : dev->dma_retry;
  dev->timeout          = dev->dma_timeout;
  dev->complete_handler = rdev_direct_complete_handler;

  PRINT_SG(dev->sg, dev->sg_n_entry, dev->id);

  retval = spd_read_sector(dev, parm.sector, parm.count, dev->sg);
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>spd_read_sector() failed(%d)", dev->id+'a', retval);
    spd_io_unlock(dev);
    goto ABORT;
  }

  return 0;

 ABORT:
  spd_send_event(dev, SPD_EVENT_DIRECT_COMPLETE, retval);

  return retval;
}


static int ioc_rdev_direct_write(spd_dev_t *dev, unsigned long arg)
{
  int i;
  int retval;
  struct p2_direct_arg parm;
  int major = dev->spec_version >> 4;
  PTRACE();

  if(unlikely(!spd_is_IOE(dev))){
    retval = -ENODEV;
    goto ABORT;
  }
  
  retval = copy_from_user(&parm, (void *)arg, sizeof(parm));
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>copy_from_user() failed(%d)", dev->id+'a', retval);
    goto ABORT;
  }

  if(parm.sg_table_size > SPD_SG_SIZE){
    PERROR("<spdr%c>invalid sg table size(%x)", dev->id+'a', parm.sg_table_size);
    retval = -EINVAL;
    goto ABORT;
  }

  if(spd_io_trylock(dev)){
    retval = -EBUSY;
    goto ABORT;
  }

  retval  = copy_from_user(dev->sg, parm.sg_table, parm.sg_table_size);
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>copy_from_user() failed(%d)", dev->id+'a', retval);
    spd_io_unlock(dev);
    goto ABORT;
  }

  spd_dma_cache_wback(dev->dev, dev->sg, parm.sg_table_size);
  dev->sg_n_entry = parm.sg_table_size/sizeof(spd_scatterlist_t);
  for(i = 0; i < dev->sg_n_entry; i++){
    spd_dma_cache_wback(dev->dev,
			bus_to_virt(le32_to_cpu((u32)dev->sg[i].bus_address)),
			le32_to_cpu(dev->sg[i].count)&(~SPD_SG_ENDMARK));
  }

  dev->retry            = (major == 4) ? SPD_NO_RETRY : dev->dma_retry;
  dev->timeout          = dev->dma_timeout;
  dev->complete_handler = rdev_direct_complete_handler;

  PRINT_SG(dev->sg, dev->sg_n_entry, dev->id);

  retval = spd_write_sector(dev, parm.sector, parm.count, dev->sg);
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>spd_write_sector() failed(%d)", dev->id+'a', retval);
    spd_io_unlock(dev);
    goto ABORT;
  }

  return 0;

 ABORT:
  spd_send_event(dev, SPD_EVENT_DIRECT_COMPLETE, retval);

  return retval;
}


static int ioc_rdev_direct_seq_write(spd_dev_t *dev, unsigned long arg)
{
  int i;
  int retval;
  struct p2_direct_arg parm;
  int major = dev->spec_version >> 4;
  PTRACE();

  if(unlikely(!spd_is_IOE(dev))){
    retval = -ENODEV;
    goto ABORT;
  }

  retval = copy_from_user(&parm, (void *)arg, sizeof(parm));
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>copy_from_user() failed(%d)", dev->id+'a', retval);
    goto ABORT;
  }
  
  if(parm.sg_table_size > SPD_SG_SIZE){
    PERROR("<spdr%c>invalid sg table size(%x)", dev->id+'a', parm.sg_table_size);
    retval = -EINVAL;
    goto ABORT;
  }

  if(spd_io_trylock(dev)){
    retval = -EBUSY;
    goto ABORT;
  }

  retval  = copy_from_user(dev->sg, parm.sg_table, parm.sg_table_size);
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>copy_from_user() failed(%d)", dev->id, retval);
    spd_io_unlock(dev);
    goto ABORT;
  }

  spd_dma_cache_wback(dev->dev, dev->sg, parm.sg_table_size);
  dev->sg_n_entry = parm.sg_table_size/sizeof(spd_scatterlist_t);
  for(i = 0; i < dev->sg_n_entry; i++){
    spd_dma_cache_wback(dev->dev,
			bus_to_virt(le32_to_cpu((u32)dev->sg[i].bus_address)),
			le32_to_cpu(dev->sg[i].count)&(~SPD_SG_ENDMARK));
  }

  dev->retry            = (major == 4) ? SPD_NO_RETRY : dev->dma_retry;
  dev->timeout          = dev->dma_timeout;
  dev->complete_handler = rdev_direct_complete_handler;

  retval = spd_seq_write_sector(dev, parm.sector, parm.count, dev->sg);
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>spd_seq_write_sector() failed(%d)", dev->id+'a', retval);
    spd_io_unlock(dev);
    goto ABORT;
  }

  return 0;

 ABORT:
  spd_send_event(dev, SPD_EVENT_DIRECT_COMPLETE, retval);

  return retval;
}


static int ioc_rdev_direct_rmw(spd_dev_t *dev, unsigned long arg)
{
  int retval;
  PTRACE();

  if(unlikely(!spd_is_IOE(dev))){
    return -ENODEV;
  }

  if(spd_io_trylock(dev)){
    return -EBUSY;
  }

  retval = spd_adpt_read_modify_write(dev, arg);
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>spd_adpt_read_modify_write() failed(%d)", dev->id+'a', retval);
    spd_io_unlock(dev);
  }

  return retval;
}


static int ioc_rdev_get_card_params(spd_dev_t *dev, unsigned long arg)
{
  int retval = 0;
  u8 *b = NULL;
  struct p2_params parm;
  int version = 0;
  u32 start = 0, end = 0;
  PTRACE();

  if(unlikely(!spd_is_IOE(dev))){
    return -ENODEV;
  }

  version = (dev->spec_version>>4);
  if(unlikely(version > 4)){
    PERROR("<spdr%c>over spec version=%d.%d",
           dev->id+'a',
           (dev->spec_version>>4), dev->spec_version&0x0f);
    return -EINVAL;
  }
  parm.p2_version = version;

  if(version >= 3){  
    if(spd_io_trylock(dev)){
      return -EBUSY;
    }

    b = dev->tmp_buf;
    dev->timeout = SPD_CMD_TIMEOUT;
    dev->retry   = SPD_NO_RETRY;
    retval = spd_log_sense(dev, 0x21, b);
    if(unlikely(retval < 0)){
      PERROR("<spdr%c>spd_log_sense() failed(%d)", dev->id+'a', retval);
      spd_io_unlock(dev);
      return retval;
    }
  }

  if(version == 4){
    unsigned long sys_area = 0;
    sys_area = dev->area[0].end + 1;
    start    = 0;
    end      = sys_area - 1;

    parm.p2_sys_start        = start;
    parm.p2_sys_sectors      = end - start + 1;
    parm.p2_protect_start    = start;
    parm.p2_protect_sectors  = end - start + 1;

    parm.p2_sys_RU_sectors   = dev->area[0].wsize;
    parm.p2_user_RU_sectors  = dev->area[1].wsize;
    parm.p2_AU_sectors       = spd_sdsta2au(b[163]);
    parm.p2_application_flag = 0xffff;
    spd_io_unlock(dev);
  } else if(version == 3){
    start = (b[37]<<24|b[38]<<16|b[39]<<8|b[40]);
    end   = (b[41]<<24|b[42]<<16|b[43]<<8|b[44]);

    parm.p2_sys_start   = start;
    parm.p2_sys_sectors = end - start + 1;

    start = (b[65]<<24|b[66]<<16|b[67]<<8|b[68]);
    end   = (b[69]<<24|b[70]<<16|b[71]<<8|b[72]);

    parm.p2_protect_start    = start;
    parm.p2_protect_sectors  = end - start + 1;

    parm.p2_sys_RU_sectors   = dev->area[0].wsize;
    parm.p2_user_RU_sectors  = dev->area[1].wsize;
    parm.p2_AU_sectors       = (b[77]<<8|b[78]);
    parm.p2_application_flag = 0xffff;
    spd_io_unlock(dev);
  } else {   
    parm.p2_sys_start        = 0;
    parm.p2_sys_sectors      = 0;
    parm.p2_protect_start    = 0;
    parm.p2_protect_sectors  = 0;
    parm.p2_sys_RU_sectors   = 0;
    parm.p2_user_RU_sectors  = SPD_WSIZE_512K;
    parm.p2_AU_sectors       = SPD_WSIZE_512K;
    parm.p2_application_flag = 0xffff;
  }

  retval = copy_to_user((void *)arg, &parm, sizeof(parm));
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>copy_to_user() failed(%d)", dev->id+'a', retval);
  }
  return retval;
}


static int ioc_rdev_set_card_params(spd_dev_t *dev, unsigned long arg)
{
  int retval = 0;
  struct p2_params parm;
  int version = 0;
  u32 start = 0, end = 0;
  PTRACE();

  if(unlikely(!spd_is_IOE(dev))){
    return -ENODEV;
  }

  version = (dev->spec_version>>4);
  if((version < 3) || (version > 4)){
    PERROR("<spdr%c>wrong spec version=%d.%d",
           dev->id+'a', version, dev->spec_version&0x0f);
    return -EINVAL;
  }

  retval = copy_from_user(&parm, (void *)arg, sizeof(parm));
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>copy_from_user() failed(%d)", dev->id+'a', retval);
    return retval;
  }
  
  start = parm.p2_sys_start;
  end   = parm.p2_sys_start + parm.p2_sys_sectors - 1;

  dev->area[0].start = start;
  dev->area[0].end   = end;
  dev->area[1].start = end + 1;
  dev->area[1].end   = end + 1 + dev->capacity;

  PINFO("<spdr%c>System Area start=%08x end=%08x ru=%08x wsize=%08x",
	dev->id+'a',
	dev->area[0].start,
	dev->area[0].end,
	dev->area[0].ausize,
	dev->area[0].wsize);

  PINFO("<spdr%c>  User Area start=%08x end=%08x au=%08x wsize=%08x",
	dev->id+'a',
	dev->area[1].start,
	dev->area[1].end,
	dev->area[1].ausize,
	dev->area[1].wsize);

  return 0;
}


static int ioc_rdev_card_initialize(spd_dev_t *dev, unsigned long arg)
{
  int version;
  int retval;
  PTRACE();

  if(unlikely(!spd_is_IOE(dev))){
    return -ENODEV;
  }

  version = (dev->spec_version>>4);
  if(version < 3){
    return -EIO;
  }

  if(spd_io_trylock(dev)){
    return -EBUSY;
  }

  dev->timeout = SPD_CMD_TIMEOUT;
  dev->retry   = SPD_NO_RETRY;
  retval = spd_card_initialize(dev);
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>spd_card_initialize() failed(%d)", dev->id+'a', retval);
  }
  spd_io_unlock(dev);
  return retval;
}


static int ioc_rdev_card_rescue(spd_dev_t *dev, unsigned long arg)
{
  int retval;
  int version;
  u8 option;
  PTRACE();

  if(unlikely(!spd_is_IOE(dev))){
    return -ENODEV;
  }

  version = dev->spec_version;
  if(version < 0x31 || 0x3F < version){ /* spec version from 3.1 to 3.15 */
    PERROR("<spdr%c>Unsupported rescue mode before spec ver.3.1(%d.%d)",
	   dev->id+'a', dev->spec_version>>4, dev->spec_version&0x0F);
    return -EINVAL;
  }

  retval = copy_from_user(&option, (void *)arg, sizeof(option));
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>copy_from_user() failed(%d)", dev->id+'a', retval);
    return retval;
  }

  if(spd_io_trylock(dev)){
    return -EBUSY;
  }

  dev->timeout = SPD_CMD_TIMEOUT;
  dev->retry   = SPD_NO_RETRY;
  retval = spd_card_rescue(dev, option);
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>spd_card_rescue() failed(%d)", dev->id+'a', retval);
  }
  spd_io_unlock(dev);
  return retval;
}


static int ioc_rdev_secured_rec(spd_dev_t *dev, unsigned long arg)
{
  int retval;
  int version;
  u8 ctrl;
  PTRACE();

  if(unlikely(!spd_is_IOE(dev))){
    return -ENODEV;
  }

  version = (dev->spec_version>>4);
  if(version != 4){
    PERROR("<spdr%c>Unsupported Secured-REC before spec ver.4.0(%d.%d)",
	   dev->id+'a', dev->spec_version>>4, dev->spec_version&0x0F);
    return -EINVAL;
  }
  retval = copy_from_user(&ctrl, (void *)arg, sizeof(ctrl));
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>copy_from_user() failed(%d)", dev->id+'a', retval);
    return retval;
  }

  if(spd_io_trylock(dev)){
    return -EBUSY;
  }

  dev->timeout = SPD_CMD_TIMEOUT;
  dev->retry   = SPD_NO_RETRY;
  retval = spd_secured_rec(dev, ctrl);
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>spd_secured_rec() failed(%d)", dev->id+'a', retval);
  }
  spd_io_unlock(dev);
  return retval;
}


static int ioc_rdev_start_rec(spd_dev_t *dev, unsigned long arg)
{
  int retval;
  int version;
  u8 id;
  PTRACE();

  if(unlikely(!spd_is_IOE(dev))){
    return -ENODEV;
  }

  version = (dev->spec_version>>4);
  if(version != 4){
    PERROR("<spdr%c>Unsupported Start-REC before spec ver.4.0(%d.%d)",
	   dev->id+'a', dev->spec_version>>4, dev->spec_version&0x0F);
    return -EINVAL;
  }
  retval = copy_from_user(&id, (void *)arg, sizeof(id));
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>copy_from_user() failed(%d)", dev->id+'a', retval);
    return retval;
  }

  if(spd_io_trylock(dev)){
    return -EBUSY;
  }

  dev->timeout = SPD_CMD_TIMEOUT;
  dev->retry   = SPD_NO_RETRY;
  retval = spd_start_rec(dev, id);
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>spd_start_rec() failed(%d)", dev->id+'a', retval);
  }
  spd_io_unlock(dev);
  return retval;
}


static int ioc_rdev_end_rec(spd_dev_t *dev, unsigned long arg)
{
  int retval;
  int version;
  u8 id;
  PTRACE();

  if(unlikely(!spd_is_IOE(dev))){
    return -ENODEV;
  }

  version = (dev->spec_version>>4);
  if(version != 4){
    PERROR("<spdr%c>Unsupported End-REC before spec ver.4.0(%d.%d)",
	   dev->id+'a', dev->spec_version>>4, dev->spec_version&0x0F);
    return -EINVAL;
  }
  retval = copy_from_user(&id, (void *)arg, sizeof(id));
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>copy_from_user() failed(%d)", dev->id+'a', retval);
    return retval;
  }

  if(spd_io_trylock(dev)){
    return -EBUSY;
  }

  dev->timeout = SPD_CMD_TIMEOUT;
  dev->retry   = SPD_NO_RETRY;
  retval = spd_end_rec(dev, id);
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>spd_end_rec() failed(%d)", dev->id+'a', retval);
  }
  spd_io_unlock(dev);
  return retval;
}


static int ioc_rdev_set_new_au(spd_dev_t *dev, unsigned long arg)
{
  int retval;
  int version;
  u8 id;
  PTRACE();

  if(unlikely(!spd_is_IOE(dev))){
    return -ENODEV;
  }

  version = (dev->spec_version>>4);
  if(version != 4){
    PERROR("<spdr%c>Unsupported Set New AU before spec ver.4.0(%d.%d)",
	   dev->id+'a', dev->spec_version>>4, dev->spec_version&0x0F);
    return -EINVAL;
  }
  retval = copy_from_user(&id, (void *)arg, sizeof(id));
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>copy_from_user() failed(%d)", dev->id+'a', retval);
    return retval;
  }

  if(spd_io_trylock(dev)){
    return -EBUSY;
  }

  dev->timeout = SPD_CMD_TIMEOUT;
  dev->retry   = SPD_NO_RETRY;
  retval = spd_set_new_au(dev, id);
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>spd_set_new_au() failed(%d)", dev->id+'a', retval);
  }
  spd_io_unlock(dev);
  return retval;
}


static int ioc_rdev_fsmi_write(spd_dev_t *dev, unsigned long arg)
{
  int retval;
  int version;
  PTRACE();

  if(unlikely(!spd_is_IOE(dev))){
    return -ENODEV;
  }

  version = (dev->spec_version>>4);
  if(version != 4){
    PERROR("<spdr%c>Unsupported FSMI-Write before spec ver.4.0(%d.%d)",
	   dev->id+'a', dev->spec_version>>4, dev->spec_version&0x0F);
    return -EINVAL;
  }
  if(spd_io_trylock(dev)){
    return -EBUSY;
  }

  retval = spd_adpt_fsmi_write(dev, arg);
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>spd_adpt_fsmi_write() failed(%d)", dev->id+'a', retval);
    spd_io_unlock(dev);
  }

  return retval;
}


static int ioc_rdev_sdcmd_write(spd_dev_t *dev, unsigned long arg)
{
  int retval;
  int version;
  PTRACE();

  if(unlikely(!spd_is_IOE(dev))){
    return -ENODEV;
  }

  version = (dev->spec_version>>4);
  if(version != 4){
    PERROR("<spdr%c>Unsupported SDCMD-Write before spec ver.4.0(%d.%d)",
	   dev->id+'a', dev->spec_version>>4, dev->spec_version&0x0F);
    return -EINVAL;
  }
  if(spd_io_trylock(dev)){
    return -EBUSY;
  }

  retval = spd_adpt_sdcmd_write(dev, arg);
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>spd_adpt_sdcmd_write() failed(%d)", dev->id+'a', retval);
    spd_io_unlock(dev);
  }

  return retval;
}


static int ioc_rdev_sd_device_reset(spd_dev_t *dev)
{
  int retval;
  int version;
  PTRACE();

  if(unlikely(!spd_is_IOE(dev))){
    return -ENODEV;
  }

  version = (dev->spec_version>>4);
  if(version != 4){
    PERROR("<spdr%c>Unsupported SD device reset before spec ver.4.0(%d.%d)",
	   dev->id+'a', dev->spec_version>>4, dev->spec_version&0x0F);
    return -EINVAL;
  }
  if(spd_io_trylock(dev)){
    return -EBUSY;
  }

  dev->timeout = SPD_CMD_TIMEOUT;
  dev->retry   = SPD_NO_RETRY;
  retval = spd_sd_device_reset(dev);
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>spd_sd_device_reset() failed(%d)", dev->id+'a', retval);
  }
  spd_io_unlock(dev);
  return retval;
}


static int ioc_rdev_go_hibernate(spd_dev_t *dev)
{
  int retval;
  int version;
  PTRACE();

  if(unlikely(!spd_is_IOE(dev))){
    return -ENODEV;
  }

  version = (dev->spec_version>>4);
  if(version != 4){
    PERROR("<spdr%c>Unsupported go hibernate before spec ver.4.0(%d.%d)",
	   dev->id+'a', dev->spec_version>>4, dev->spec_version&0x0F);
    return -EINVAL;
  }
  if(spd_io_trylock(dev)){
    return -EBUSY;
  }

  dev->timeout = SPD_CMD_TIMEOUT;
  dev->retry   = SPD_NO_RETRY;
  retval = spd_go_hibernate(dev);
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>spd_go_hibernate() failed(%d)", dev->id+'a', retval);
  }
  spd_io_unlock(dev);
  return retval;
}


static int ioc_rdev_table_recover(spd_dev_t *dev, unsigned long arg)
{
  int retval;
  int version;
  u8 ver;
  PTRACE();

  if(unlikely(!spd_is_IOE(dev))){
    return -ENODEV;
  }

  version = (dev->spec_version>>4);
  if(version != 4){
    PERROR("<spdr%c>Unsupported table recover before spec ver.4.0(%d.%d)",
	   dev->id+'a', dev->spec_version>>4, dev->spec_version&0x0F);
    return -EINVAL;
  }
  retval = copy_from_user(&ver, (void *)arg, sizeof(ver));
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>copy_from_user() failed(%d)", dev->id+'a', retval);
    return retval;
  }

  if(spd_io_trylock(dev)){
    return -EBUSY;
  }

  dev->timeout = SPD_CMD_TIMEOUT;
  dev->retry   = SPD_NO_RETRY;
  retval = spd_table_recover(dev, ver);
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>spd_table_recover() failed(%d)", dev->id+'a', retval);
  }
  spd_io_unlock(dev);
  return retval;
}


static int ioc_rdev_au_erase(spd_dev_t *dev, unsigned long arg)
{
  int retval;
  int version;
  struct p2_blk_erase_arg parm;
  PTRACE();

  if(unlikely(!spd_is_IOE(dev))){
    return -ENODEV;
  }

  version = (dev->spec_version>>4);
  if(version != 4){
    PERROR("<spd%c>Unsupported AU Erase before spec ver.4.0(%d.%d)",
	   dev->id+'a', dev->spec_version>>4, dev->spec_version&0x0F);
    return -EINVAL;
  }
  retval = copy_from_user(&parm, (void *)arg, sizeof(parm));
  if(unlikely(retval < 0)){
    PERROR("<spd%c>copy_from_user() failed(%d)", dev->id+'a', retval);
    return retval;
  }

  if(spd_io_trylock(dev)){
    return -EBUSY;
  }

  dev->timeout = SPD_CMD_LONG_TIMEOUT;
  dev->retry   = SPD_NO_RETRY;
  retval = spd_au_erase(dev, parm.sector, parm.count);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>spd_au_erase() failed(%d)", dev->id+'a', retval);
  }
  spd_io_unlock(dev);
  return retval;
}


static int ioc_rdev_lmg(spd_dev_t *dev, unsigned long arg)
{
  int retval;
  int version;
  struct p2_sdcmd_d_arg parm;
  PTRACE();

  if(unlikely(!spd_is_IOE(dev))){
    return -ENODEV;
  }

  version = (dev->spec_version>>4);
  if(version != 4){
    PERROR("<spdr%c>Unsupported LMG before spec ver.4.0(%d.%d)",
	   dev->id+'a', dev->spec_version>>4, dev->spec_version&0x0F);
    return -EINVAL;
  }
  retval = copy_from_user(&parm, (void *)arg, sizeof(parm));
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>copy_from_user() failed(%d)", dev->id+'a', retval);
    return retval;
  }

  if(spd_io_trylock(dev)){
    return -EBUSY;
  }

  dev->timeout = SPD_CMD_TIMEOUT;
  dev->retry   = SPD_NO_RETRY;
  retval = spd_lmg(dev, parm.sd_arg, (u8 *)parm.buf);
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>spd_lmg() failed(%d)", dev->id+'a', retval);
  }
  spd_io_unlock(dev);
  return retval;
}


static int ioc_rdev_get_sst(spd_dev_t *dev, unsigned long arg)
{
  int retval;
  int version;
  struct p2_sdcmd_d_arg parm;
  PTRACE();

  if(unlikely(!spd_is_IOE(dev))){
    return -ENODEV;
  }

  version = (dev->spec_version>>4);
  if(version != 4){
    PERROR("<spdr%c>Unsupported Get SST before spec ver.4.0(%d.%d)",
	   dev->id+'a', dev->spec_version>>4, dev->spec_version&0x0F);
    return -EINVAL;
  }
  retval = copy_from_user(&parm, (void *)arg, sizeof(parm));
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>copy_from_user() failed(%d)", dev->id+'a', retval);
    return retval;
  }

  if(spd_io_trylock(dev)){
    return -EBUSY;
  }

  dev->timeout = SPD_CMD_TIMEOUT;
  dev->retry   = SPD_NO_RETRY;
  retval = spd_get_sst(dev, (u8 *)dev->tmp_buf);
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>spd_get_sst() failed(%d)", dev->id+'a', retval);
    spd_io_unlock(dev);
    return retval;
  }

  retval = copy_to_user((void *)parm.buf, dev->tmp_buf, SPD_HARDSECT_SIZE);
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>copy_to_user() failed(%d)", dev->id+'a', retval);
  }
  spd_io_unlock(dev);
  return retval;
}


static int ioc_rdev_get_dinfo(spd_dev_t *dev, unsigned long arg)
{
  int retval;
  int version;
  struct p2_sdcmd_d_arg parm;
  PTRACE();

  if(unlikely(!spd_is_IOE(dev))){
    return -ENODEV;
  }

  version = (dev->spec_version>>4);
  if(version != 4){
    PERROR("<spdr%c>Unsupported Get DINFO before spec ver.4.0(%d.%d)",
	   dev->id+'a', dev->spec_version>>4, dev->spec_version&0x0F);
    return -EINVAL;
  }
  retval = copy_from_user(&parm, (void *)arg, sizeof(parm));
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>copy_from_user() failed(%d)", dev->id+'a', retval);
    return retval;
  }

  if(spd_io_trylock(dev)){
    return -EBUSY;
  }

  dev->timeout = SPD_CMD_TIMEOUT;
  dev->retry   = SPD_NO_RETRY;
  retval = spd_get_dinfo(dev, parm.sd_arg, (u8 *)dev->tmp_buf);
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>spd_get_dinfo() failed(%d)", dev->id+'a', retval);
    spd_io_unlock(dev);
    return retval;
  }

  retval = copy_to_user((void *)parm.buf, dev->tmp_buf, SPD_HARDSECT_SIZE);
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>copy_to_user() failed(%d)", dev->id+'a', retval);
  }
  spd_io_unlock(dev);
  return retval;
}


static int ioc_rdev_dmg(spd_dev_t *dev, unsigned long arg)
{
  int retval;
  int version;
  struct p2_sdcmd_d_arg parm;
  PTRACE();

  if(unlikely(!spd_is_IOE(dev))){
    return -ENODEV;
  }

  version = (dev->spec_version>>4);
  if(version != 4){
    PERROR("<spdr%c>Unsupported DMG before spec ver.4.0(%d.%d)",
	   dev->id+'a', dev->spec_version>>4, dev->spec_version&0x0F);
    return -EINVAL;
  }
  retval = copy_from_user(&parm, (void *)arg, sizeof(parm));
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>copy_from_user() failed(%d)", dev->id+'a', retval);
    return retval;
  }

  if(spd_io_trylock(dev)){
    return -EBUSY;
  }

  dev->timeout = SPD_CMD_TIMEOUT;
  dev->retry   = SPD_NO_RETRY;
  retval = spd_dmg(dev, parm.sd_arg, (u8 *)parm.buf);
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>spd_dmg() failed(%d)", dev->id+'a', retval);
  }
  spd_io_unlock(dev);
  return retval;
}


static int ioc_rdev_esw(spd_dev_t *dev, unsigned long arg)
{
  int retval;
  int version;
  u8 sw;
  PTRACE();

  if(unlikely(!spd_is_IOE(dev))){
    return -ENODEV;
  }

  version = (dev->spec_version>>4);
  if(version != 4){
    PERROR("<spdr%c>Unsupported ESW before spec ver.4.0(%d.%d)",
	   dev->id+'a', dev->spec_version>>4, dev->spec_version&0x0F);
    return -EINVAL;
  }
  retval = copy_from_user(&sw, (void *)arg, sizeof(sw));
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>copy_from_user() failed(%d)", dev->id+'a', retval);
    return retval;
  }

  if(spd_io_trylock(dev)){
    return -EBUSY;
  }

  dev->timeout = SPD_CMD_TIMEOUT;
  dev->retry   = SPD_NO_RETRY;
  retval = spd_esw(dev, sw);
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>spd_esw() failed(%d)", dev->id+'a', retval);
    spd_io_unlock(dev);
    return retval;
  }
  spd_io_unlock(dev);

  if(spd_is_LK(dev)){
    unsigned long flags = 0;

    spin_lock_irqsave(&dev->lock, flags);
    PINFO("<spd%c>clear LK, set DEB and send event:CARD READY", dev->id+'a');
    spd_clr_LK(dev);
    spd_set_DEB(dev);
    spin_unlock_irqrestore(&dev->lock, flags);

    spd_send_event(dev, SPD_EVENT_CARD_READY, 0);
  }
  return retval;
}


static int ioc_rdev_get_ph(spd_dev_t *dev, unsigned long arg)
{
  int retval;
  int version;
  struct p2_sdcmd_d_arg parm;
  PTRACE();

  if(unlikely(!spd_is_IOE(dev))){
    return -ENODEV;
  }

  version = (dev->spec_version>>4);
  if(version != 4){
    PERROR("<spdr%c>Unsupported Get PH before spec ver.4.0(%d.%d)",
	   dev->id+'a', dev->spec_version>>4, dev->spec_version&0x0F);
    return -EINVAL;
  }
  retval = copy_from_user(&parm, (void *)arg, sizeof(parm));
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>copy_from_user() failed(%d)", dev->id+'a', retval);
    return retval;
  }

  if(spd_io_trylock(dev)){
    return -EBUSY;
  }

  dev->timeout = SPD_CMD_TIMEOUT;
  dev->retry   = SPD_NO_RETRY;
  retval = spd_get_ph(dev, parm.sd_arg, (u8 *)dev->tmp_buf);
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>spd_get_ph() failed(%d)", dev->id+'a', retval);
    spd_io_unlock(dev);
    return retval;
  }

  retval = copy_to_user((void *)parm.buf, dev->tmp_buf, SPD_HARDSECT_SIZE);
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>copy_to_user() failed(%d)", dev->id+'a', retval);
  }
  spd_io_unlock(dev);
  return retval;
}


static int ioc_rdev_get_linfo(spd_dev_t *dev, unsigned long arg)
{
  int retval;
  int version;
  struct p2_sdcmd_d_arg parm;
  PTRACE();

  if(unlikely(!spd_is_IOE(dev))){
    return -ENODEV;
  }

  version = (dev->spec_version>>4);
  if(version != 4){
    PERROR("<spdr%c>Unsupported Get LINFO before spec ver.4.0(%d.%d)",
	   dev->id+'a', dev->spec_version>>4, dev->spec_version&0x0F);
    return -EINVAL;
  }
  retval = copy_from_user(&parm, (void *)arg, sizeof(parm));
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>copy_from_user() failed(%d)", dev->id+'a', retval);
    return retval;
  }

  if(spd_io_trylock(dev)){
    return -EBUSY;
  }

  dev->timeout = SPD_CMD_TIMEOUT;
  dev->retry   = SPD_NO_RETRY;
  retval = spd_get_linfo(dev, parm.sd_arg, (u8 *)dev->tmp_buf);
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>spd_get_linfo() failed(%d)", dev->id+'a', retval);
    spd_io_unlock(dev);
    return retval;
  }

  retval = copy_to_user((void *)parm.buf, dev->tmp_buf, SPD_HARDSECT_SIZE);
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>copy_to_user() failed(%d)", dev->id+'a', retval);
  }
  spd_io_unlock(dev);
  return retval;
}


static int ioc_rdev_dau(spd_dev_t *dev, unsigned long arg)
{
  int retval;
  int version;
  struct p2_sdcmd_d_arg parm;
  PTRACE();

  if(unlikely(!spd_is_IOE(dev))){
    return -ENODEV;
  }

  version = (dev->spec_version>>4);
  if(version != 4){
    PERROR("<spdr%c>Unsupported DAU before spec ver.4.0(%d.%d)",
	   dev->id+'a', dev->spec_version>>4, dev->spec_version&0x0F);
    return -EINVAL;
  }
  retval = copy_from_user(&parm, (void *)arg, sizeof(parm));
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>copy_from_user() failed(%d)", dev->id+'a', retval);
    return retval;
  }

  if(spd_io_trylock(dev)){
    return -EBUSY;
  }

  dev->timeout = SPD_CMD_TIMEOUT;
  dev->retry   = SPD_NO_RETRY;
  retval = spd_dau(dev, parm.sd_arg, (u8 *)parm.buf);
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>spd_dau() failed(%d)", dev->id+'a', retval);
  }
  spd_io_unlock(dev);
  return retval;
}


static int ioc_rdev_set_dparam(spd_dev_t *dev, unsigned long arg)
{
  int retval;
  int version;
  struct p2_sdcmd_d_arg parm;
  PTRACE();

  if(unlikely(!spd_is_IOE(dev))){
    return -ENODEV;
  }

  version = (dev->spec_version>>4);
  if(version != 4){
    PERROR("<spdr%c>Unsupported SET DPARAM before spec ver.4.0(%d.%d)",
	   dev->id+'a', dev->spec_version>>4, dev->spec_version&0x0F);
    return -EINVAL;
  }
  retval = copy_from_user(&parm, (void *)arg, sizeof(parm));
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>copy_from_user() failed(%d)", dev->id+'a', retval);
    return retval;
  }

  if(spd_io_trylock(dev)){
    return -EBUSY;
  }

  dev->timeout = SPD_CMD_TIMEOUT;
  dev->retry   = SPD_NO_RETRY;
  retval = spd_set_dparam(dev, parm.sd_arg, (u8 *)parm.buf);
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>spd_set_dparam() failed(%d)", dev->id+'a', retval);
  }
  spd_io_unlock(dev);
  return retval;
}


static int ioc_rdev_get_phs(spd_dev_t *dev, unsigned long arg)
{
  int retval;
  int version;
  struct p2_sdcmd_d_arg parm;
  PTRACE();

  if(unlikely(!spd_is_IOE(dev))){
    return -ENODEV;
  }

  version = (dev->spec_version>>4);
  if(version != 4){
    PERROR("<spdr%c>Unsupported Get PHS before spec ver.4.0(%d.%d)",
	   dev->id+'a', dev->spec_version>>4, dev->spec_version&0x0F);
    return -EINVAL;
  }
  retval = copy_from_user(&parm, (void *)arg, sizeof(parm));
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>copy_from_user() failed(%d)", dev->id+'a', retval);
    return retval;
  }

  if(spd_io_trylock(dev)){
    return -EBUSY;
  }

  dev->timeout = SPD_CMD_TIMEOUT;
  dev->retry   = SPD_NO_RETRY;
  retval = spd_get_phs(dev, parm.sd_arg, (u8 *)dev->tmp_buf);
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>spd_get_phs() failed(%d)", dev->id+'a', retval);
    spd_io_unlock(dev);
    return retval;
  }

  retval = copy_to_user((void *)parm.buf, dev->tmp_buf, SPD_HARDSECT_SIZE);
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>copy_to_user() failed(%d)", dev->id+'a', retval);
  }
  spd_io_unlock(dev);
  return retval;
}


static int ioc_rdev_set_aut_status(spd_dev_t *dev, unsigned long arg)
{
  int retval = 0;
  unsigned long status = 0;
  PTRACE();

  retval = copy_from_user(&status, (void *)arg, sizeof(status));
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>copy_from_user() failed(%d)", dev->id+'a', retval);
    return retval;
  }

  if(status&SPD_AUT_STATUS_ERROR){
    unsigned long flags = 0;
    p2_slot_image mask = 1<<dev->id;

    PINFO("<spdr%c>set SPD_ADPT_AUT_ERROR", dev->id+'a');
    spd_set_AUE(dev);

    PINFO("<spdr%c>clear open_request flag", dev->id+'a');
    spin_lock_irqsave(&rdev_status_lock, flags);
    rdev_card_status.open_request &= ~mask;
    spin_unlock_irqrestore(&rdev_status_lock, flags);
  }

  return 0;
}


static int ioc_rdev_preprocess_io_retry(spd_dev_t *dev, unsigned long arg)
{
  PTRACE();

  if(unlikely(!spd_is_IOE(dev))){
    return -ENODEV;
  }

  if(spd_is_status(dev, SPD_WRITE_GUARD)){
    spd_clr_status(dev, SPD_WRITE_GUARD);
  }

  return 0;
}
