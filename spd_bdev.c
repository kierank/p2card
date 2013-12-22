/*
 P2card block device modules
 $Id: spd_bdev.c 223 2006-09-19 11:03:44Z hiraoka $
 */

#define SPD_MINOR_SHIFT   4
#define SPD_N_PARTITONS   (1<<SPD_MINOR_SHIFT)

static int spd_major;
#define MAJOR_NR          spd_major
#define DEVICE_NAME       "spd"
#define DEVICE_NR(device) (MINOR(device) >> SPD_MINOR_SHIFT)

#if defined(CONFIG_IOSCHED_P2IOFilter)
# define SPD_IOSCHEDULER "p2IoFilter"
#else
# define SPD_IOSCHEDULER "anticipatory"
#endif /* CONFIG_IOSCHED_P2IOFilter */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/blkdev.h>
#include <linux/bio.h>
#include <linux/hdreg.h>
#include <linux/genhd.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <asm/uaccess.h>

#include "spd.h"
#include "spd_bdev.h"

#line __LINE__ "spd_bdev.c" /* Replace full path(__FILE__) to spd_bdev.c. */


static void bdev_event_handler(spd_dev_t *dev, u32 event, unsigned long arg);
static int  bdev_add_disk(spd_dev_t *dev);
static int  bdev_del_disk(spd_dev_t *dev);
static int  bdev_attach(spd_dev_t *dev);
static int  bdev_detach(spd_dev_t *dev);
#if (KERNEL_VERSION(2,6,20) <= LINUX_VERSION_CODE) /* KERNEL_VERSION : 2.6.20 - */
static void bdev_attach_tasklet(struct work_struct *work);
#else /* KERNEL_VERSION : - 2.6.19 */
static void bdev_attach_tasklet(unsigned long arg);
#endif /* KERNEL_VERSION : 2.6.20 - */

static int  bdev_open(struct inode *inode, struct file *file);
static int  bdev_release(struct inode *inode, struct file *file);
static int  bdev_ioctl(struct inode *inode, struct file *file,
		       unsigned int cmd, unsigned long arg);
static int  bdev_media_changed(struct gendisk *gd);
static int  bdev_revalidate_disk(struct gendisk *gd);
static int  bdev_getgeo(struct block_device *bd, struct hd_geometry *geo);
static int  bdev_mount(struct inode *inode);

static void bdev_request(struct request_queue *queue);
static void bdev_next_request_tasklet(unsigned long arg);
void bdev_end_request(spd_dev_t *dev, int errcode);
static int  bdev_transfer(spd_dev_t *dev);

static int  bdev_make_sg(spd_dev_t *dev);
static int  bdev_read(spd_dev_t *dev);
static void bdev_read_complete_handler(spd_dev_t *dev);
static int  bdev_write(spd_dev_t *dev);
static void bdev_write_complete_handler(spd_dev_t *dev);
static int  bdev_read_modify_write(spd_dev_t *dev);
static void bdev_rmw_read_complete_handler(spd_dev_t *dev);
static void bdev_rmw_write_complete_handler(spd_dev_t *dev);
static int  bdev_copy_sector(spd_dev_t *dev);
static int  bdev_cache_release(spd_dev_t *dev);
static void bdev_cache_write_complete_handler(spd_dev_t *dev);

static int  ioc_hdio_getgeo(struct block_device *bd, unsigned long arg);
static int  ioc_bdev_block_erase(spd_dev_t *dev, unsigned long arg);
static int  ioc_bdev_check_protect(spd_dev_t *dev, unsigned long arg);
static int  ioc_bdev_log_sense(spd_dev_t *dev, unsigned long arg);
static int  ioc_bdev_get_card_params(spd_dev_t *dev, unsigned int cmd,
				     struct block_device *bd, unsigned long arg);
static int  ioc_bdev_card_initialize(spd_dev_t *dev, unsigned long arg);
static int  ioc_bdev_identify_device(spd_dev_t *dev, unsigned long arg);
static int  ioc_bdev_card_terminate(spd_dev_t *dev, unsigned long arg);
static int  ioc_bdev_card_rescue(spd_dev_t *dev, unsigned long arg);

static int  ioc_bdev_secured_rec(spd_dev_t *dev, unsigned long arg);
static int  ioc_bdev_start_rec(spd_dev_t *dev, unsigned long arg);
static int  ioc_bdev_end_rec(spd_dev_t *dev, unsigned long arg);
static int  ioc_bdev_sd_device_reset(spd_dev_t *dev);
static int  ioc_bdev_go_hibernate(spd_dev_t *dev);
static int  ioc_bdev_table_recover(spd_dev_t *dev, unsigned long arg);
static int  ioc_bdev_au_erase(spd_dev_t *dev, unsigned long arg);

static int  ioc_bdev_lmg(spd_dev_t *dev, unsigned long arg);
static int  ioc_bdev_get_sst(spd_dev_t *dev, unsigned long arg);
static int  ioc_bdev_get_dinfo(spd_dev_t *dev, unsigned long arg);
static int  ioc_bdev_dmg(spd_dev_t *dev, unsigned long arg);
static int  ioc_bdev_esw(spd_dev_t *dev, unsigned long arg);
static int  ioc_bdev_get_ph(spd_dev_t *dev, unsigned long arg);
static int  ioc_bdev_get_linfo(spd_dev_t *dev, unsigned long arg);
static int  ioc_bdev_dau(spd_dev_t *dev, unsigned long arg);
static int  ioc_bdev_set_dparam(spd_dev_t *dev, unsigned long arg);
static int  ioc_bdev_get_phs(spd_dev_t *dev, unsigned long arg);
static int  ioc_bdev_set_aut_status(spd_dev_t *dev, unsigned long arg);


static struct block_device_operations bops = {
  .owner           = THIS_MODULE,
  .open            = bdev_open,
  .release         = bdev_release,
  .ioctl           = bdev_ioctl,
  .revalidate_disk = bdev_revalidate_disk,
  .media_changed   = bdev_media_changed,
#if (KERNEL_VERSION(2,6,16) <= LINUX_VERSION_CODE) /* KERNEL_VERSION : 2.6.16 - */
  .getgeo          = bdev_getgeo,
#endif /* KERNEL_VERSION : 2.6.16 - */
#if defined(CONFIG_P2PF)
  .mount_fs        = bdev_mount,
#endif /* CONFIG_P2PF */
};

static spd_bdev_t spd_bdev[SPD_N_DEV];

struct list_head bdev_cache_wait_list;
spinlock_t bdev_cache_wait_list_lock;


int spd_bdev_init(void)
{
  int retval;
  int i;
  spd_dev_t *dev;
  PTRACE();

  MAJOR_NR = SPD_BDEV_MAJOR;
  retval = register_blkdev(MAJOR_NR, DEVICE_NAME);
  if(unlikely(retval < 0)){
    PERROR("register_blkdev() failed(%d)", retval);
    return retval;
  }
  if(MAJOR_NR == 0){
    MAJOR_NR = retval;
  }

  memset(spd_bdev, 0, sizeof(spd_bdev));
  for(i = 0; i < SPD_N_DEV; i++){
    dev       = &spd_dev[i];
    dev->bdev = &spd_bdev[i];

    INIT_LIST_HEAD(&dev->bdev->list);
    dev->bdev->event_handler = bdev_event_handler;
    dev->bdev->event_mask = (SPD_EVENT_CARD_READY |
			     SPD_EVENT_CARD_REMOVE|
			     SPD_EVENT_IO_UNLOCKED|
			     SPD_EVENT_DISK_ENABLE|
			     SPD_EVENT_DISK_CLOSE);

    dev->devnum       = MKDEV(MAJOR_NR, (i<<SPD_MINOR_SHIFT));
    dev->bdev->usage  = 0;

    dev->bdev->media_changed = 0;	

    dev->bdev->req   = NULL;
    dev->bdev->gd    = NULL;
    dev->bdev->queue = NULL;

    dev->bdev->tasklet.next  = NULL;
    dev->bdev->tasklet.state = 0;
    atomic_set(&dev->bdev->tasklet.count, 0);
    dev->bdev->tasklet.func  = bdev_next_request_tasklet;
    dev->bdev->tasklet.data  = (unsigned long)dev;

    dev->bdev->rmw_sector      = (u32)-1;
    dev->bdev->rmw_count       = 0;
    dev->bdev->rmw_sg          = NULL;
    dev->bdev->rmw_sg_offset   = 0;
    dev->bdev->n_map           = 0;
    dev->bdev->rmw_bio         = NULL;
    dev->bdev->rmw_bio_offset  = 0;
    dev->bdev->rmw_bio_idx     = 0;
    dev->bdev->rmw_bvec_offset = 0;
	
#if (KERNEL_VERSION(2,6,20) <= LINUX_VERSION_CODE) /* KERNEL_VERSION : 2.6.20 - */
    INIT_WORK(&dev->wq, bdev_attach_tasklet);
#else /* KERNEL_VERSION : - 2.6.19 */
    INIT_WORK(&dev->wq, (void (*)(void *))bdev_attach_tasklet, dev);
#endif /* KERNEL_VERSION : 2.6.20 - */
  }

  return 0;
}


int spd_bdev_exit(void)
{
  int i;
  spd_dev_t *dev;
  PTRACE();

  for(i = 0; i < SPD_N_DEV; i++){
    dev = &spd_dev[i];
    bdev_del_disk(dev);
  }
  unregister_blkdev(MAJOR_NR, DEVICE_NAME);

  return 0;
}


static int bdev_add_disk(spd_dev_t *dev)
{
  struct gendisk *gd;
  struct request_queue *queue;
  u16 max_sectors;
  int retval;
  int version;
  PTRACE();

  if(spd_is_GDE(dev)){
    if(unlikely(!dev->bdev->gd)){
      PERROR("<spd%c>multiple add disk", dev->id+'a');
      return -EFAULT;
    }
    PINFO("<spd%c>Disable disk", dev->id+'a');
    bdev_del_disk(dev);
  }

  gd = alloc_disk(SPD_N_PARTITONS);
  if(unlikely(gd == NULL)){
    PERROR("<spd%c>alloc_disk() failed", dev->id+'a');
    return -ENOMEM;
  }

  spin_lock_init(&dev->bdev->rq_lock);
  queue = blk_init_queue(bdev_request, &dev->bdev->rq_lock);
  if(unlikely(queue == NULL)){
    PERROR("<spd%c>blk_init_queue() failed", dev->id+'a');
    put_disk(gd);
    return -ENOMEM;
  }

  dev->bdev->queue = queue;
  dev->bdev->gd    = gd;

  version = dev->spec_version>>4;
  if(version == 4){
    max_sectors = dev->area[dev->n_area-1].wsize;
  } else if(version == 3){
    max_sectors = SPD_WSIZE_4M;
  } else {
    max_sectors = SPD_WSIZE_2M;
  }
  PINFO("<spd%c>max_sectors=%x", dev->id+'a', max_sectors);

  blk_queue_hardsect_size    (queue, SPD_HARDSECT_SIZE);
  blk_queue_max_sectors      (queue, max_sectors);
  blk_queue_max_phys_segments(queue, SPD_SG_N_ENTRY);
  blk_queue_max_hw_segments  (queue, SPD_SG_N_ENTRY);
  blk_queue_max_segment_size (queue, SPD_SG_MAX_COUNT);
  queue->queuedata = dev;

  /* Change I/O scheduler
   * (ref. s390/block/dasc.c and s390/char/tape_block.c) */
  elevator_exit(queue->elevator);
  retval = elevator_init(queue, SPD_IOSCHEDULER);
  if(unlikely(retval)){
    PERROR("<spd%c>elevator_init() failed", dev->id+'a');
    blk_cleanup_queue(queue);
    put_disk(gd);
    return retval;
  }
  PINFO("<spd%c>Change I/O scheduler: %s", dev->id+'a', SPD_IOSCHEDULER);

  gd->major        = MAJOR_NR;
  gd->first_minor  = (dev->id<<SPD_MINOR_SHIFT);
  gd->fops         = &bops;
  gd->queue        = queue;
  gd->flags        = (GENHD_FL_REMOVABLE);
  gd->private_data = dev;
  snprintf(gd->disk_name, 
	   sizeof(gd->disk_name), DEVICE_NAME"%c", 'a'+dev->id);

  HOTPLUG_OUT(24);
  retval = bdev_revalidate_disk(gd);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>bdev_revalidate_disk() failed(%d)", dev->id+'a', retval);
    put_disk(gd);
    return retval;
  }

  add_disk(gd);
  HOTPLUG_OUT(25);
  spd_send_event(dev, SPD_EVENT_DISK_ENABLE, 0);
  HOTPLUG_OUT(26);

  return 0;
}


static int bdev_del_disk(spd_dev_t *dev)
{
  struct gendisk *gd;
  struct request_queue *queue;
  unsigned long flags = 0;
  PTRACE();

  spin_lock_irqsave(&dev->lock, flags);
  gd    = dev->bdev->gd;
  queue = dev->bdev->queue;

  dev->bdev->gd    = NULL;
  dev->bdev->queue = NULL;
  spin_unlock_irqrestore(&dev->lock, flags);

  if(gd != NULL){
    if(spd_is_GDE(dev)){
      del_gendisk(gd);
      spd_clr_GDE(dev);
    }
    put_disk(gd);
  }

  if(queue != NULL){
    blk_cleanup_queue(queue);
  }

  spd_send_event(dev, SPD_EVENT_DISK_DISABLE, 0);

  return 0;
}


static void bdev_event_handler(spd_dev_t *dev, u32 event, unsigned long arg)
{
  PTRACE();

  switch(event){
  case SPD_EVENT_CARD_READY:
    {
      if(!spd_is_LK(dev)){
	bdev_attach(dev);
      }
      break;
    }

  case SPD_EVENT_CARD_REMOVE:
    {
      bdev_detach(dev);
      break;
    }

  case SPD_EVENT_IO_UNLOCKED:
    {
      if(spd_is_GDE(dev)){
	tasklet_hi_schedule(&dev->bdev->tasklet);
      }
      break;
    }

  case SPD_EVENT_DISK_ENABLE:
    {
      if(spd_is_DEB(dev)){
	spd_clr_DEB(dev);
      }
      break;
    }

  case SPD_EVENT_DISK_CLOSE:
    {
      if(spd_is_DAR(dev) && !spd_is_LK(dev)){
	schedule_work(&dev->wq);
      }
      break;
    }
  }
}


#if (KERNEL_VERSION(2,6,20) <= LINUX_VERSION_CODE) /* KERNEL_VERSION : 2.6.20 - */
static void bdev_attach_tasklet(struct work_struct *work)
{
  spd_dev_t *dev = container_of(work, spd_dev_t, wq);
  PTRACE();

  if(unlikely(!dev)){
    PERROR("unknown device");
    BUG();
  }
  bdev_attach(dev);
}

#else /* KERNEL_VERSION : - 2.6.19 */

static void bdev_attach_tasklet(unsigned long arg)
{
  spd_dev_t *dev = (spd_dev_t *)arg;
  PTRACE();

  if(unlikely(!dev)){
    PERROR("unknown device");
    BUG();
  }
  bdev_attach(dev);
}
#endif /* KERNEL_VERSION : 2.6.20 - */


static int bdev_attach(spd_dev_t *dev)
{
  int retval;
  unsigned long flags = 0;
  PTRACE();

  spin_lock_irqsave(&dev->lock, flags);
  spd_clr_DAR(dev);
  spin_unlock_irqrestore(&dev->lock, flags);

  spd_cache_invalidate(dev);
  
  if(!spd_is_DWM(dev)){
    retval = bdev_add_disk(dev);
    if(unlikely(retval < 0)){
      PERROR("<spd%c>bdev_add_disk() failed(%d)", dev->id+'a', retval);
      return retval;
    }
  }

  return 0;
}


static int bdev_detach(spd_dev_t *dev)
{
  PTRACE();

  if(spd_is_status(dev, SPD_DISK_OPEN)){
    PINFO("<spd%c>DWM enable", dev->id+'a');
    spd_set_DWM(dev);
  } else {
    bdev_del_disk(dev);
  }

  return 0;
}


static int bdev_open(struct inode *inode, struct file *file)
{
  spd_dev_t *dev = inode->i_bdev->bd_disk->private_data;
  unsigned long flags = 0;
  PTRACE();

  HOTPLUG_OUT(28);
  if(unlikely(!dev)){
    PERROR("unknown device number(%d)", DEVICE_NR(inode->i_rdev));
    return -ENODEV;
  }
  
  file->private_data = dev;

  if(!spd_is_IOE(dev)||!spd_is_GDE(dev)){
    PERROR("<spd%c> is disable", dev->id+'a');
    return -ENODEV;
  }
  HOTPLUG_OUT(29);
  spin_lock_irqsave(&dev->lock, flags);
  dev->bdev->usage++;
  spd_set_status(dev, SPD_DISK_OPEN);
  HOTPLUG_OUT(30);
  spin_unlock_irqrestore(&dev->lock, flags);

#if ! defined(CONFIG_P2PF)
  bdev_mount(inode);
#endif /* ! CONFIG_P2PF */

  return 0;
}


static int bdev_release(struct inode *inode, struct file *file)
{
  spd_dev_t *dev = inode->i_bdev->bd_disk->private_data;
  unsigned long flags = 0;
  PTRACE();

  HOTPLUG_OUT(33);
  if(unlikely(!dev)){
    PERROR("unknown device number(%d)", DEVICE_NR(inode->i_rdev));
    return -ENODEV;
  }

  spin_lock_irqsave(&dev->lock, flags);
  HOTPLUG_OUT(34);
  dev->bdev->usage--;
  if(dev->bdev->usage == 0){
    PINFO("<spd%c>usage count=0", dev->id+'a');
    spin_unlock_irqrestore(&dev->lock, flags);
    HOTPLUG_OUT(35);
    invalidate_partition(dev->bdev->gd, DEVICE_NR(inode->i_rdev));
    HOTPLUG_OUT(36);

    if(spd_is_IOE(dev)&&spd_is_DWM(dev)){
      spd_clr_DWM(dev);
      PINFO("<spd%c>DWM disable", dev->id+'a');
      bdev_del_disk(dev);
      if(!spd_is_LK(dev)){
	spd_set_DAR(dev);
      }
    } else {
      spd_clr_DWM(dev);
    }

    HOTPLUG_OUT(37);
    spin_lock_irqsave(&dev->lock, flags);
    spd_clr_status(dev, SPD_DISK_OPEN);
    HOTPLUG_OUT(38);
  }
  HOTPLUG_OUT(39);
  spin_unlock_irqrestore(&dev->lock, flags);
  HOTPLUG_OUT(40);
  spd_send_event(dev, SPD_EVENT_DISK_CLOSE, dev->bdev->usage);
  HOTPLUG_OUT(41);

  return 0;
}


static int bdev_mount(struct inode *inode)
{
  spd_dev_t *dev = inode->i_bdev->bd_disk->private_data;
  PTRACE();

  if(unlikely(!dev)){
    PERROR("unknown device number(%d)", DEVICE_NR(inode->i_rdev));
    return -ENODEV;
  }
  
  if(unlikely(!spd_is_IOE(dev)||!spd_is_GDE(dev))){
    PERROR("<spd%c> is disable", dev->id+'a');
    return -ENODEV;
  }
  HOTPLUG_OUT(31);
  spd_send_event(dev, SPD_EVENT_DISK_OPEN, dev->bdev->usage);
  HOTPLUG_OUT(32);

  return 0;
}


static int bdev_ioctl(struct inode *inode, struct file *file,
		      unsigned int cmd, unsigned long arg)
{
  int retval;
  spd_dev_t *dev = inode->i_bdev->bd_disk->private_data;
  PTRACE();
  HOTPLUG_OUT(42);

  if(unlikely(!dev)){
    PERROR("unknown device number(%d)", DEVICE_NR(inode->i_rdev));
    return -ENODEV;
  }

  if(!spd_is_IOE(dev)){
    return -ENODEV;
  }
  HOTPLUG_OUT(43);    
  switch(cmd){
  case HDIO_GETGEO:
    {
      retval = ioc_hdio_getgeo(inode->i_bdev, arg);
      break;
    }

  case P2_COMMAND_BLK_ERASE:
    {
      retval = ioc_bdev_block_erase(dev, arg);
      break;
    }

  case P2_COMMAND_AU_ERASE:
    {
      retval = ioc_bdev_au_erase(dev, arg);
      break;
    }

  case P2_CHECK_WRITE_PROTECT:
    {
      retval = ioc_bdev_check_protect(dev, arg);
      break;
    }

  case P2_COMMAND_LOG_SENSE:
    {
      retval = ioc_bdev_log_sense(dev, arg);
      break;
    }

  case P2_GET_CARD_PARAMS:
  case P2_KERNEL_GET_CARD_PARAMS:
    {
      retval = ioc_bdev_get_card_params(dev, cmd, inode->i_bdev, arg);
      break;
    }

  case P2_COMMAND_CARD_INITIALIZE:
    {
      retval = ioc_bdev_card_initialize(dev, arg);
      break;
    }

  case P2_COMMAND_IDENTIFY_DEVICE:
    {
      retval = ioc_bdev_identify_device(dev, arg);
      break;
    }

  case P2_TERMINATE:
    {
      retval = ioc_bdev_card_terminate(dev, arg);
      break;
    }

  case P2_COMMAND_CARD_RESCUE:
    {
      retval = ioc_bdev_card_rescue(dev, arg);
      break;
    }

  case P2_COMMAND_SECURED_REC:
    {
      retval = ioc_bdev_secured_rec(dev, arg);
      break;
    }

  case P2_COMMAND_START_REC:
    {
      retval = ioc_bdev_start_rec(dev, arg);
      break;
    }

  case P2_COMMAND_END_REC:
    {
      retval = ioc_bdev_end_rec(dev, arg);
      break;
    }

  case P2_COMMAND_SD_DEVICE_RESET:
    {
      retval = ioc_bdev_sd_device_reset(dev);
      break;
    }

  case P2_COMMAND_GO_HIBERNATE:
    {
      retval = ioc_bdev_go_hibernate(dev);
      break;
    }

  case P2_COMMAND_TABLE_RECOVER:
    {
      retval = ioc_bdev_table_recover(dev, arg);
      break;
    }

  case P2_COMMAND_LMG:
    {
      retval = ioc_bdev_lmg(dev, arg);
      break;
    }

  case P2_COMMAND_GET_SST:
    {
      retval = ioc_bdev_get_sst(dev, arg);
      break;
    }

  case P2_COMMAND_GET_DINFO:
    {
      retval = ioc_bdev_get_dinfo(dev, arg);
      break;
    }

  case P2_COMMAND_DMG:
    {
      retval = ioc_bdev_dmg(dev, arg);
      break;
    }

  case P2_COMMAND_ESW:
    {
      retval = ioc_bdev_esw(dev, arg);
      break;
    }

  case P2_COMMAND_GET_PH:
    {
      retval = ioc_bdev_get_ph(dev, arg);
      break;
    }

  case P2_COMMAND_GET_LINFO:
    {
      retval = ioc_bdev_get_linfo(dev, arg);
      break;
    }

  case P2_COMMAND_DAU:
    {
      retval = ioc_bdev_dau(dev, arg);
      break;
    }

  case P2_COMMAND_SET_DPARAM:
    {
      retval = ioc_bdev_set_dparam(dev, arg);
      break;
    }

  case P2_COMMAND_GET_PHS:
    {
      retval = ioc_bdev_get_phs(dev, arg);
      break;
    }

  case P2_SET_AUT_STATUS:
    {
      retval = ioc_bdev_set_aut_status(dev, arg);
      break;
    }

  default:
    {
      retval = -ENOTTY; 
    }
  }
  HOTPLUG_OUT(44);

  return retval;
}


static int bdev_media_changed(struct gendisk *gd)
{
  spd_dev_t *dev;
  int media_changed;
  PTRACE();

  dev = gd->private_data;
  media_changed = dev->bdev->media_changed;
  dev->bdev->media_changed = 0;

  return media_changed;
}


static int bdev_revalidate_disk(struct gendisk *gd)
{
  spd_dev_t *dev;
  sector_t capacity;
  PTRACE();

  dev = gd->private_data;
  if(spd_is_LK(dev)) {
    PINFO("<spd%c>cannot revalidate at LK status", dev->id+'a');
    return 0;
  }
  spd_clr_GDE(dev);

  if(spd_io_lock(dev) < 0){
    return -EINTR;
  }

  capacity = spd_read_capacity(dev);
  if(unlikely(capacity <= 0)){
    PERROR("<spd%c>spd_read_capacity() failed(%d)", dev->id+'a', (int)capacity);
    spd_io_unlock(dev);
    return (capacity ? capacity : -ENODEV);
  }

  spd_io_unlock(dev);
  set_capacity(gd, capacity);
  spd_set_GDE(dev);

  spd_send_event(dev, SPD_EVENT_DISK_OPEN, dev->bdev->usage); /* ! */
  return 0;
}


static int bdev_getgeo(struct block_device *bd, struct hd_geometry *geo)
{
  spd_dev_t *dev;
  int retval;
  u16 *buf;
  PTRACE();

  dev = bd->bd_disk->private_data;

  retval = spd_io_lock(dev);
  if(retval < 0){
    return -EINTR;
  }
  buf = (u16 *)dev->tmp_buf;
  dev->timeout = SPD_CMD_TIMEOUT;
  dev->retry   = SPD_NO_RETRY;
  retval = spd_identify_device(dev, buf);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>spd_identify_device() failed(%d)", 'a'+dev->id, retval);
    spd_io_unlock(dev);
    return retval;
  }

  geo->cylinders = buf[1];
  geo->heads     = buf[3];
  geo->sectors   = buf[6];

  spd_io_unlock(dev);

  PINFO("<spd%c>HDIO_GETGEO:start=%lu, cylinders=%d, heads=%d, sectors=%d",
	dev->id+'a', geo->start, geo->cylinders, geo->heads, geo->sectors);

  return 0;
}


static inline void bdev_cache_wait_list_add(spd_dev_t *dev)
{
  /* This function is dead!? */
  unsigned long flags = 0;
  PTRACE();

  spin_lock_irqsave(&bdev_cache_wait_list_lock, flags);
  list_add(&dev->bdev->list, &bdev_cache_wait_list);
  spin_unlock_irqrestore(&bdev_cache_wait_list_lock, flags);
}


static inline void bdev_wakeup_cache_wait(void)
{
  /* This function is dead!? */
  unsigned long flags = 0;
  spd_bdev_t *bdev;
  PTRACE();

  spin_lock_irqsave(&bdev_cache_wait_list_lock, flags);
  if(!list_empty(&bdev_cache_wait_list)){
    bdev = list_entry(bdev_cache_wait_list.next, spd_bdev_t, list);
    list_del_init(&bdev->list);
    spin_unlock_irqrestore(&bdev_cache_wait_list_lock, flags);
    tasklet_hi_schedule(&bdev->tasklet);
  } else {
    spin_unlock_irqrestore(&bdev_cache_wait_list_lock, flags);
  }
}


static void bdev_request(struct request_queue *queue)
{
  int retval;
  spd_dev_t *dev;
  struct request *req;
  PTRACE();

  if(unlikely(NULL == queue)){
    PINFO("queue is NULL");
    return;
  }
  dev = (spd_dev_t *)queue->queuedata;

  req = elv_next_request(queue);
  if(req == NULL){
    PINFO("rq is NULL");

    if(dev->cache != NULL){
      bdev_cache_release(dev);
    }
    return;
  }
  PINFO("rq(%s) sector=%08lx count=%04lx",
	(rq_data_dir(req)==WRITE?"WRITE":"READ"),
	(unsigned long)req->sector, req->nr_sectors);

  if(rq_data_dir(req) == READ && dev->cache != NULL){
    bdev_cache_release(dev);
    return;
  }

  if(spd_io_trylock(dev)){
    PINFO("<spd%c>spd_io_trylock() failed", dev->id+'a');
    return;
  }

  if(dev->cache == NULL){
    dev->cache = spd_cache_alloc(dev);
  }

  if(dev->cache == NULL){
    PINFO("<spd%c>cache alloc failed", dev->id+'a');
    /* bdev_cache_wait_list_add(dev); */
    spd_io_unlock(dev);
    return;
  }

  blkdev_dequeue_request(req);
  dev->bdev->req = req;

  if(spd_is_DWM(dev)){
    PINFO("<spd%c> bdev_req under DWM", dev->id+'a');
    dev->bdev->hard_nr_sectors = req->hard_nr_sectors;
    bdev_end_request(dev, -ENODEV);
    spd_io_unlock(dev);
    return;
  }

  if(spd_is_status(dev, SPD_WRITE_GUARD) && rq_data_dir(req) == WRITE){
    PINFO("<spd%c>WRITE GUARD MODE", dev->id+'a');
    spd_send_event(dev, SPD_EVENT_CARD_ERROR, -EIO);
    bdev_end_request(dev, -EIO);
    spd_io_unlock(dev);
    return;
  }

  spin_unlock_irq(&dev->bdev->rq_lock);
  retval = bdev_transfer(dev);
  spin_lock_irq(&dev->bdev->rq_lock);

  if(retval < 0){
    PERROR("<spd%c>spd_transfer() failed(%d)", dev->id+'a', retval);
  }
}


static void bdev_next_request_tasklet(unsigned long arg)
{
  unsigned long flags = 0;
  spd_dev_t *dev = (spd_dev_t *)arg;
  PTRACE();

  if(spd_is_GDE(dev) && !spd_is_DEB(dev)){
    spin_lock_irqsave(&dev->bdev->rq_lock, flags);
    bdev_request(dev->bdev->queue);
    spin_unlock_irqrestore(&dev->bdev->rq_lock, flags);
  }
}


void bdev_end_request(spd_dev_t *dev, int errcode)
{
  int uptodate;
  struct _spd_bdev_private_t *bdev = dev->bdev;
  struct request *req = bdev->req;
  PTRACE();

  if(unlikely(req == NULL)){
    BUG();
  }

  bdev->req = NULL;

#if (KERNEL_VERSION(2,6,25) <= LINUX_VERSION_CODE) /* KERNEL_VERSION : 2.6.25 - */
  if(errcode < 0){
    uptodate = -EIO;
    req->errors = -1;
    req->cmd_flags |= REQ_QUIET; /* Disable end_request error messages */
  } else {
    uptodate = 0;
    req->errors = 0;
  }

  if(spd_is_DWM(dev) && rq_data_dir(req) == WRITE){
    uptodate = 0;
    req->errors = -1;
    req->cmd_flags |= REQ_QUIET;
  }
#else /* KERNEL_VERSION : - 2.6.24 */
  if(errcode < 0){
    uptodate = 0;
    req->errors = -1;
# if (KERNEL_VERSION(2,6,19) <= LINUX_VERSION_CODE) /* KERNEL_VERSION : 2.6.19 - */
    req->cmd_flags |= REQ_QUIET;
# else /* KERNEL_VERSION : - 2.6.18 */
    req->flags |= REQ_QUIET;
# endif /* KERNEL_VERSION : 2.6.19 - */
  } else {
    uptodate = 1;
    req->errors = 0;
  }

  if(spd_is_DWM(dev) && rq_data_dir(req) == WRITE){
    uptodate = 1;
    req->errors = -1;
# if (KERNEL_VERSION(2,6,19) <= LINUX_VERSION_CODE) /* KERNEL_VERSION : 2.6.19 - */
    req->cmd_flags |= REQ_QUIET;
# else /* KERNEL_VERSION : - 2.6.18 */
    req->flags |= REQ_QUIET;
# endif /* KERNEL_VERSION : 2.6.19 - */
  }
#endif /* KERNEL_VERSION : 2.6.25 - */

  if(bdev->n_map && likely(dev->dev)){
    int dir;
    PINFO("<spd%c>dma_unmap_sg() n=%d", dev->id+'a', dev->sg_n_entry);

    dir = (rq_data_dir(req) == WRITE) ? DMA_TO_DEVICE : DMA_FROM_DEVICE;
    dma_unmap_sg(dev->dev, bdev->sg, dev->sg_n_entry, dir);
    bdev->n_map = 0;
  }

#if (KERNEL_VERSION(2,6,25) <= LINUX_VERSION_CODE) /* KERNEL_VERSION : 2.6.25 - */
  while(unlikely(__blk_end_request(req, uptodate, bdev->hard_nr_sectors*SPD_HARDSECT_SIZE)));
#else /* KERNEL_VERSION : - 2.6.24 */
  while(unlikely(end_that_request_first(req, uptodate, bdev->hard_nr_sectors)));
# if (KERNEL_VERSION(2,6,15) <= LINUX_VERSION_CODE) /* KERNEL_VERSION : 2.6.15 - */
  end_that_request_last(req, uptodate);
# else /* KERNEL_VERSION : - 2.6.14 */
  end_that_request_last(req);
# endif /* KERNEL_VERSION : 2.6.15 - */
#endif /* KERNEL_VERSION : 2.6.25 - */
}


static int bdev_transfer(spd_dev_t *dev)
{
  u32 sector;
  int retval = 0;
  u32 wsize;
  struct request *req = dev->bdev->req;
  PTRACE();

  switch(rq_data_dir(req)){
  case READ:
    {
      if(rq_is_drct(req)){
        retval = drct_read(dev);
      } else {
        retval = bdev_read(dev);
      }
      break;
    }

  case WRITE:
    {
      sector = req->sector;
      wsize = spd_get_wsize(dev, sector);
      if(wsize < 0) {
        PERROR("<spd%c>spd_get_wsize() failed sector=%08x", dev->id+'a', sector);
        return -EINVAL;
      }

      if(rq_is_seq(req)){
        PINFO("sequencial write request sector=%08x count=%08x",
              sector, (u32)req->hard_nr_sectors);
      }
	  
      if((sector - spd_get_sector_offset(dev, sector)) % wsize == 0 && req->hard_nr_sectors == wsize){
        if(rq_is_drct(req)){
          retval = drct_write(dev);
        } else {
          retval = bdev_write(dev);
        }
      } else { /* Read Modify Write */
        if(rq_is_drct(req)){
          retval = drct_read_modify_write(dev);
        } else {
          retval = bdev_read_modify_write(dev);
        }
      }
      break;
    }

  default:
    {
      PERROR("<spd%c>invalid request command(%d)", dev->id+'a', (int)rq_data_dir(req));
      retval = -EINVAL;
    }
  }

  return retval;
}


static int bdev_make_sg(spd_dev_t *dev)
{
  struct request *req = dev->bdev->req;
  spd_scatterlist_t *hwif_sg = dev->sg;
  struct scatterlist *sg = dev->bdev->sg;
  int n, i, dir;
  PTRACE();

  n = blk_rq_map_sg(dev->bdev->queue, req, sg);
  if(unlikely(n > SPD_SG_N_ENTRY)){
    PERROR("<spd%c>scatterlist overflow(%d)", dev->id+'a', n);
    return -ENOMEM;
  }

  dir = (rq_data_dir(req) == WRITE) ? DMA_TO_DEVICE : DMA_FROM_DEVICE;
  dev->bdev->n_map = dma_map_sg(dev->dev, sg, n, dir);
  PINFO("<spd%c>n=%d, n_map=%d", dev->id+'a', n, dev->bdev->n_map);

  for(i = 0; i < n; i++){
    hwif_sg[i].bus_address = cpu_to_le32(sg_dma_address(&sg[i]));
    hwif_sg[i].count       = cpu_to_le32(sg_dma_len(&sg[i]));
  }

  hwif_sg[n-1].count |= cpu_to_le32(SPD_SG_ENDMARK);
  dev->sg_n_entry = n;
  spd_dma_cache_wback(dev->dev, hwif_sg, sizeof(spd_scatterlist_t)*n);

  PRINT_SG(hwif_sg, n, dev->id);

  return 0;
}


static int bdev_read(spd_dev_t *dev)
{
  int retval;
  u32 sector;
  u16 count;
  unsigned long flags = 0;
  struct request *req = dev->bdev->req;
  PTRACE();

  dev->bdev->hard_nr_sectors = req->hard_nr_sectors;

  retval = bdev_make_sg(dev);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>bdev_make_sg() failed(%d)", dev->id+'a', retval);
    goto ABORT;
  }

  sector                = req->sector;
  count                 = req->hard_nr_sectors;
  dev->retry            = dev->dma_retry;
  dev->timeout          = dev->dma_timeout;
  dev->complete_handler = bdev_read_complete_handler;
  retval = spd_read_sector(dev, sector, count, dev->sg);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>spd_read_sector() failed(%d)", dev->id+'a', retval);
    goto ABORT;
  }

  return 0;

ABORT:
  spin_lock_irqsave(&dev->bdev->rq_lock, flags);
  bdev_end_request(dev, retval);
  spin_unlock_irqrestore(&dev->bdev->rq_lock, flags);

  dev->complete_handler = NULL;
  spd_io_unlock(dev);

  return retval;
}


static void bdev_read_complete_handler(spd_dev_t *dev)
{
  unsigned long flags = 0;
  PTRACE();

  PINFO("<spd%c>complete time=%dms errcode=%d",
	dev->id+'a', jiffies_to_msecs(dev->ticks), dev->errcode);

  spin_lock_irqsave(&dev->bdev->rq_lock, flags);
  bdev_end_request(dev, dev->errcode);
  spin_unlock_irqrestore(&dev->bdev->rq_lock, flags);

  dev->complete_handler = NULL;
  spd_io_unlock(dev);
}


static int bdev_write(spd_dev_t *dev)
{
  int retval;
  u32 sector;
  u16 count;
  unsigned long flags = 0;
  struct request *req = dev->bdev->req;
  PTRACE();

  dev->bdev->hard_nr_sectors = req->hard_nr_sectors;

  retval = bdev_make_sg(dev);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>bdev_make_sg() failed(%d)", dev->id+'a', retval);
    goto ABORT;
  }

  sector = req->sector;
  count  = req->hard_nr_sectors;

  if(sector == dev->cache->sector){
    dev->cache->sector = (u32)-1;
    spd_cache_clr_dirty(dev->cache);
  }

  dev->retry   = dev->dma_retry;
  dev->timeout = dev->dma_timeout;
  dev->complete_handler = bdev_write_complete_handler;

  retval = spd_write_sector(dev, sector, count, dev->sg);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>spd_write_sector() failed(%d)", dev->id+'a', retval);
    goto ABORT;
  }

  return 0;

ABORT:
  spin_lock_irqsave(&dev->bdev->rq_lock, flags);
  bdev_end_request(dev, retval);
  spin_unlock_irqrestore(&dev->bdev->rq_lock, flags);

  dev->complete_handler = NULL;
  spd_io_unlock(dev);

  return retval;
}


static void bdev_write_complete_handler(spd_dev_t *dev)
{
  unsigned long flags = 0;
  PTRACE();

  PINFO("<spd%c>complete time=%dms errcode=%d",
	dev->id+'a', jiffies_to_msecs(dev->ticks), dev->errcode);

  spin_lock_irqsave(&dev->bdev->rq_lock, flags);
  bdev_end_request(dev, dev->errcode);
  spin_unlock_irqrestore(&dev->bdev->rq_lock, flags);

  dev->complete_handler = NULL;
  spd_io_unlock(dev);
}


static int bdev_read_modify_write(spd_dev_t *dev)
{
  unsigned long flags = 0;
  int retval;
  u32 sector;
  int wsize;
  struct request *req = dev->bdev->req;
  PTRACE();

  dev->bdev->hard_nr_sectors = req->hard_nr_sectors;
  dev->bdev->rmw_sector      = req->sector;
  dev->bdev->rmw_count       = req->hard_nr_sectors;
  dev->bdev->rmw_sg          = dev->bdev->sg;
  dev->bdev->rmw_sg_offset   = 0;

  dev->timeout = dev->dma_timeout;
  dev->retry   = dev->dma_retry;

  retval = blk_rq_map_sg(dev->bdev->queue, req, dev->bdev->rmw_sg);
  if(unlikely(retval > SPD_SG_N_ENTRY)){
    PERROR("<spd%c>scatterlist overflow(%d)", dev->id+'a', retval);
    retval = -ENOMEM;
    goto ABORT;
  }

  sector = dev->bdev->rmw_sector;
  wsize = spd_get_wsize(dev, sector);
  if(unlikely(wsize < 0)){
    PERROR("<spd%c>spd_get_wsize() failed sector=%08x",
           dev->id+'a', sector);
    retval = -EINVAL;
    goto ABORT;
  }
  sector = align_sector(sector, wsize, spd_get_sector_offset(dev, sector));

  if(sector == dev->cache->sector){
    bdev_copy_sector(dev);
    if(dev->bdev->rmw_count == 0){
      PINFO("<spd%c>rmw_count=0!", dev->id+'a');
      spin_lock_irqsave(&dev->bdev->rq_lock, flags);
      bdev_end_request(dev, 0);
      spin_unlock_irqrestore(&dev->bdev->rq_lock, flags);

      dev->complete_handler = NULL;
      spd_io_unlock(dev);

      return 0;
    }

    dev->complete_handler = bdev_rmw_write_complete_handler;
    spd_cache_prepare(dev, SPD_DIR_WRITE);
    retval = spd_write_sector(dev,
                              dev->cache->sector,
			      dev->cache->n_sector,
                              dev->cache->sg);
    if(unlikely(retval < 0)){
      PERROR("<spd%c>spd_write_sector() failed(%d)", dev->id+'a', retval);
      goto ABORT;
    }

    return retval;
  }

  if(spd_cache_is_dirty(dev->cache)){
    dev->complete_handler = bdev_rmw_write_complete_handler;
    spd_cache_prepare(dev, SPD_DIR_WRITE);
    retval = spd_write_sector(dev,
                              dev->cache->sector,
                              dev->cache->n_sector,
                              dev->cache->sg);
    if(unlikely(retval < 0)){
      PERROR("<spd%c>spd_write_sector() failed(%d)", dev->id+'a', retval);
      goto ABORT;
    }

    return retval;
  }

  if(sector == dev->bdev->rmw_sector && dev->bdev->rmw_count >= wsize){
    dev->cache->sector   = sector;
    dev->cache->n_sector = wsize;
    bdev_copy_sector(dev);

    if(dev->bdev->rmw_count == 0){
      spin_lock_irqsave(&dev->bdev->rq_lock, flags);
      bdev_end_request(dev, 0);
      spin_unlock_irqrestore(&dev->bdev->rq_lock, flags);

      dev->complete_handler = NULL;
      spd_io_unlock(dev);
      return 0;
    }
 
    dev->complete_handler = bdev_rmw_write_complete_handler;
    spd_cache_prepare(dev, SPD_DIR_WRITE);
    retval = spd_write_sector(dev,
                              dev->cache->sector,
                              dev->cache->n_sector,
                              dev->cache->sg);
    if(unlikely(retval < 0)){
      PERROR("<spd%c>spd_write_sector() failed(%d)", dev->id+'a', retval);
      goto ABORT;
    }

    return retval;
  }

  dev->cache->sector    = sector;
  dev->cache->n_sector  = wsize;
  dev->complete_handler = bdev_rmw_read_complete_handler;
  spd_cache_prepare(dev, SPD_DIR_READ);
  retval = spd_read_sector(dev, 
                           dev->cache->sector,
                           dev->cache->n_sector,
                           dev->cache->sg);

  if(unlikely(retval < 0)){
    PERROR("<spd%c>spd_read_sector() failed(%d)", dev->id+'a', retval);
    goto ABORT;
  }

  return retval;
  
ABORT:
  PINFO("<spd%c>ABORT at %s", dev->id+'a', __FUNCTION__);
  if(dev->cache){
    dev->cache->sector = (u32)-1;
    spd_cache_clr_dirty(dev->cache);
  }

  spin_lock_irqsave(&dev->bdev->rq_lock, flags);
  bdev_end_request(dev, retval);
  spin_unlock_irqrestore(&dev->bdev->rq_lock, flags);  

  dev->complete_handler = NULL;
  spd_io_unlock(dev);

  return retval;
}


static void bdev_rmw_read_complete_handler(spd_dev_t *dev)
{
  int retval;
  unsigned long flags = 0;
  PTRACE();

  PINFO("<spd%c>complete time=%dms errcode=%d",
	dev->id+'a', jiffies_to_msecs(dev->ticks), dev->errcode);

  if(unlikely(dev->errcode < 0)){
    retval = dev->errcode;
    goto ABORT;
  }

  bdev_copy_sector(dev);

  if(dev->bdev->rmw_count == 0){
    spin_lock_irqsave(&dev->bdev->rq_lock, flags);
    bdev_end_request(dev, 0);
    spin_unlock_irqrestore(&dev->bdev->rq_lock, flags);

    dev->complete_handler = NULL;
    spd_io_unlock(dev);

    return;
  }

  dev->complete_handler = bdev_rmw_write_complete_handler;
  spd_cache_prepare(dev, SPD_DIR_WRITE);
  retval = spd_write_sector(dev,
                            dev->cache->sector,
                            dev->cache->n_sector,
                            dev->cache->sg);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>spd_write_sector() failed(%d)", dev->id+'a', retval);
    goto ABORT;
  }

  return;
  
ABORT:
  PINFO("<spd%c>ABORT at %s", dev->id+'a', __FUNCTION__);
  if(dev->cache){
    dev->cache->sector = (u32)-1;
    spd_cache_clr_dirty(dev->cache);
  }

  spin_lock_irqsave(&dev->bdev->rq_lock, flags);
  bdev_end_request(dev, retval);
  spin_unlock_irqrestore(&dev->bdev->rq_lock, flags);

  dev->complete_handler = NULL;
  spd_io_unlock(dev);
}


static void bdev_rmw_write_complete_handler(spd_dev_t *dev)
{
  int retval = 0;
  unsigned long flags = 0;
  u32 sector;
  int wsize;
  PTRACE();

  PINFO("<spd%c>complete time=%dms errcode=%d",
	dev->id+'a', jiffies_to_msecs(dev->ticks), dev->errcode);

  if(dev->cache){
    spd_cache_clr_dirty(dev->cache);
  }

  if(unlikely(dev->errcode < 0)){
    retval = dev->errcode;
    goto ABORT;
  }

  sector = dev->bdev->rmw_sector;
  wsize  = spd_get_wsize(dev, sector);
  if(unlikely(wsize < 0)){
    PERROR("<spd%c>spd_get_wsize() failed sector=%08x", dev->id+'a', sector);
    retval = -EINVAL;
    goto ABORT;
  }
  sector = align_sector(sector, wsize, spd_get_sector_offset(dev, sector));

  if(sector == dev->bdev->rmw_sector && dev->bdev->rmw_count >= wsize){
    dev->cache->sector   = sector;
    dev->cache->n_sector = wsize;
    bdev_copy_sector(dev);

    if(dev->bdev->rmw_count == 0){
      spin_lock_irqsave(&dev->bdev->rq_lock, flags);
      bdev_end_request(dev, 0);
      spin_unlock_irqrestore(&dev->bdev->rq_lock, flags);

      dev->complete_handler = NULL;
      spd_io_unlock(dev);

      return;
    }

    dev->complete_handler = bdev_rmw_write_complete_handler;
    spd_cache_prepare(dev, SPD_DIR_WRITE);
    retval = spd_write_sector(dev,
                              dev->cache->sector,
                              dev->cache->n_sector,
                              dev->cache->sg);
    if(unlikely(retval < 0)){
      PERROR("<spd%c>spd_write_sector() failed(%d)", dev->id+'a', retval);
      goto ABORT;
    }

    return;
  }

  dev->cache->sector   = sector;
  dev->cache->n_sector = wsize;

  dev->complete_handler = bdev_rmw_read_complete_handler;
  spd_cache_prepare(dev, SPD_DIR_READ);  
  retval = spd_read_sector(dev,
                           dev->cache->sector,
                           dev->cache->n_sector,
                           dev->cache->sg);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>spd_read_sector() failed(%d)", dev->id+'a', retval);
    goto ABORT;
  }

  return;
  
ABORT:
  PINFO("<spd%c>ABORT at %s", dev->id+'a', __FUNCTION__);
  if(dev->cache){
    dev->cache->sector = (u32)-1;
    spd_cache_clr_dirty(dev->cache);
  }

  spin_lock_irqsave(&dev->bdev->rq_lock, flags);
  bdev_end_request(dev, retval);
  spin_unlock_irqrestore(&dev->bdev->rq_lock, flags);

  dev->complete_handler = NULL;
  spd_io_unlock(dev);
}


static int bdev_copy_sector(spd_dev_t *dev)
{
  u32 offset, count;
  u32 bi_size;
  u32 size;
  u8  *virt_addr;
  struct scatterlist *sg;
  PTRACE();

  PRINT_COPY_CACHE(dev);
  PINFO("<spd%c>cache_sector=%08x, rmw_sector=%08x, rmw_count=%04x",
	dev->id+'a', dev->cache->sector, dev->bdev->rmw_sector, dev->bdev->rmw_count);
  PCOMMAND("<spd%c>COPY_CACHE id=%02d, cache_sector=%08x, rmw_sector=%08x, rmw_count=%04x",
           dev->id+'a',
           dev->cache->id,
           dev->cache->sector,
           dev->bdev->rmw_sector,
           dev->bdev->rmw_count);

  spd_cache_set_dirty(dev->cache);

  offset = (dev->bdev->rmw_sector - dev->cache->sector);
  if(offset + dev->bdev->rmw_count > dev->cache->n_sector){
    count = dev->cache->n_sector - offset;
    dev->bdev->rmw_count -= count;
    dev->bdev->rmw_sector = dev->cache->sector+dev->cache->n_sector;
  } else {
    count = dev->bdev->rmw_count;
    dev->bdev->rmw_count  = 0;
    dev->bdev->rmw_sector = (u32)-1;
  };

  offset *= SPD_HARDSECT_SIZE;
  size    = count*SPD_HARDSECT_SIZE;
  sg      = dev->bdev->rmw_sg;

  while(size > 0){
    unsigned long flags = 0;

    local_irq_save(flags);
    virt_addr = kmap_atomic(sg_page(sg), KM_IRQ0) + sg->offset + dev->bdev->rmw_sg_offset;
    bi_size   = sg_dma_len(sg) - dev->bdev->rmw_sg_offset;

    if(bi_size > size){
      PINFO("## bi_size=%lx size=%lx", (unsigned long)bi_size, (unsigned long)size);
      dev->bdev->rmw_sg_offset += size;
      spd_cache_copy(dev->cache, offset, virt_addr, size);
      kunmap_atomic(sg_page(sg), KM_IRQ0);
      local_irq_restore(flags);
      return 0;
    }
    spd_cache_copy(dev->cache, offset, virt_addr, bi_size);
    offset += bi_size;
    size -= bi_size;

    kunmap_atomic(sg_page(sg), KM_IRQ0);
    local_irq_restore(flags);
    sg++;
    dev->bdev->rmw_sg = sg;
    dev->bdev->rmw_sg_offset = 0;
  }

  return 0;
}


static int bdev_cache_release(spd_dev_t *dev)
{
  spd_cache_t *cache = dev->cache;
  int retval;
  PTRACE();

  if(cache == NULL){
    return 0;
  }
  
  if(spd_io_trylock(dev)){
    return -EBUSY;
  }

  if(!spd_cache_is_dirty(cache)){
    dev->cache = NULL;
    spd_cache_release(dev, cache);
    spd_io_unlock(dev);
    /* bdev_wakeup_cache_wait(); */

    return 0;
  }

  PRINT_WRITE_CACHE(dev);

  dev->timeout = dev->dma_timeout;
  dev->retry   = dev->dma_retry;
  dev->complete_handler = bdev_cache_write_complete_handler;
  spd_cache_prepare(dev, SPD_DIR_WRITE);
  retval = spd_write_sector(dev, 
                            cache->sector,
                            cache->n_sector,
                            cache->sg);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>spd_write_sector() failed(%d)", dev->id+'a', retval);
    spd_cache_clr_dirty(cache);
    dev->cache = NULL;
    spd_cache_release(dev, cache);
    dev->complete_handler = NULL;
    spd_io_unlock(dev);
    /* bdev_wakeup_cache_wait(); */

    return retval;
  }

  return 0;
}


static void bdev_cache_write_complete_handler(spd_dev_t *dev)
{
  spd_cache_t *cache = dev->cache;
  PTRACE();

  if(unlikely(cache == NULL)){
    PERROR("<spd%c>cache is NULL!", dev->id+'a');
    dev->complete_handler = NULL;
    spd_io_unlock(dev);
    return;
  }

  PINFO("<spd%c>write cache complete time=%dms errcode=%d",
	dev->id+'a', jiffies_to_msecs(dev->ticks), dev->errcode);
  if(unlikely(dev->errcode < 0)){
    PERROR("<spd%c>write cache failed(%d) sector=%08x count=%04x",
           dev->id+'a',
           dev->errcode, cache->sector, cache->n_sector);
  }
  spd_cache_clr_dirty(cache);
  dev->cache = NULL;
  spd_cache_release(dev, cache);
  dev->complete_handler = NULL;
  spd_io_unlock(dev);
  /* bdev_wakeup_cache_wait(); */
}


static int ioc_hdio_getgeo(struct block_device *bd, unsigned long arg)
{
  spd_dev_t *dev;
  int retval;
  struct hd_geometry geo;
  PTRACE();

  dev = bd->bd_disk->private_data;

  retval = bdev_getgeo(bd, &geo);
  if(unlikely(retval < 0)){
    return retval;
  }

  retval = copy_to_user((void *)arg, &geo, sizeof(geo));
  if(unlikely(retval < 0)){
    PERROR("<spd%c>copy_to_user() failed(%d)", dev->id+'a', retval);
    return retval;
  }

  return 0;
}


static int ioc_bdev_block_erase(spd_dev_t *dev, unsigned long arg)
{
  int retval;
  struct p2_blk_erase_arg parm;
  PTRACE();

  retval = copy_from_user(&parm, (void *)arg, sizeof(parm));
  if(unlikely(retval < 0)){
    PERROR("<spd%c>copy_from_user() failed(%d)", dev->id+'a', retval);
    return retval;
  }

  retval = spd_io_lock(dev);
  if(retval < 0){
    return -EINTR;
  }

  dev->timeout = SPD_CMD_LONG_TIMEOUT;
  dev->retry   = SPD_NO_RETRY;
  retval = spd_block_erase(dev, parm.sector, parm.count);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>spd_block_erase() failed(%d)", dev->id+'a', retval);
    spd_io_unlock(dev);
    return retval;
  }

  spd_io_unlock(dev);
  return retval;
}


static int ioc_bdev_au_erase(spd_dev_t *dev, unsigned long arg)
{
  int retval;
  int version;
  struct p2_blk_erase_arg parm;
  PTRACE();

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

  retval = spd_io_lock(dev);
  if(retval < 0){
    return -EINTR;
  }

  dev->timeout = SPD_CMD_LONG_TIMEOUT;
  dev->retry   = SPD_NO_RETRY;
  retval = spd_au_erase(dev, parm.sector, parm.count);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>spd_au_erase() failed(%d)", dev->id+'a', retval);
    spd_io_unlock(dev);
    return retval;
  }

  spd_io_unlock(dev);
  return retval;
}


static int ioc_bdev_check_protect(spd_dev_t *dev, unsigned long not_used)
{
  int retval;
  PTRACE();

  if(spd_is_status(dev, SPD_WRITE_GUARD)){
    return 1;
  }

  retval = spd_io_lock(dev);
  if(retval < 0){
    return -EINTR;
  }

  dev->timeout = SPD_CMD_TIMEOUT;
  dev->retry   = SPD_NO_RETRY;
  retval = spd_check_protect(dev);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>spd_check_protect() failed(%d)", dev->id+'a', retval);
    spd_io_unlock(dev);
    return retval;
  }
  spd_io_unlock(dev);

  return retval;
}


static int ioc_bdev_log_sense(spd_dev_t *dev, unsigned long arg)
{
  int retval;
  struct p2_log_sense_arg parm;
  PTRACE();

  if(unlikely(!spd_is_IOE(dev))){
    return -ENODEV;
  }

  retval = copy_from_user(&parm, (void *)arg, sizeof(parm));
  if(unlikely(retval < 0)){
    PERROR("<spd%c>copy_from_user() failed(%d)", dev->id+'a', retval);
    return retval;
  }

  retval = spd_io_lock(dev);
  if(retval < 0){
    return -EINTR;
  }

  dev->timeout = SPD_CMD_LONG_TIMEOUT; /* ! */
  dev->retry   = SPD_NO_RETRY;
  retval = spd_log_sense(dev, parm.page, (u8 *)dev->tmp_buf);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>spd_log_sense() failed(%d)", dev->id+'a', retval);
    spd_io_unlock(dev);
    return retval;
  }

  retval = copy_to_user((void *)parm.buffer, dev->tmp_buf, SPD_HARDSECT_SIZE);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>copy_to_user() failed(%d)", dev->id+'a', retval);
  }
  spd_io_unlock(dev);
  return retval;
}


static int ioc_bdev_get_card_params(spd_dev_t *dev, unsigned int cmd,
                                    struct block_device *bd, unsigned long arg)
{
  int retval = 0;
  u8 *b = NULL;
  struct p2_params parm;
  int version = 0;
  u32 start = 0, end = 0, sectors = 0;
  u32 part_start = 0, part_end = 0, part_sectors = 0;
  struct hd_struct *partition = NULL;
  PTRACE();

  version = (dev->spec_version>>4);
  if(unlikely(version > 4)){
    PERROR("<spd%c>over spec version=%d.%d",
           dev->id+'a',
           (dev->spec_version>>4), dev->spec_version&0x0f);
    return -EINVAL;
  }
  parm.p2_version = version;

  if(version >= 3){
    retval = spd_io_lock(dev);
    if(retval < 0){
      return -EINTR;
    }

    b = dev->tmp_buf;
    dev->timeout = SPD_CMD_TIMEOUT;
    dev->retry   = SPD_NO_RETRY;
    retval = spd_log_sense(dev, 0x21, b);
    if(unlikely(retval < 0)){
      PERROR("<spd%c>spd_log_sense() failed(%d)", dev->id+'a', retval);
      spd_io_unlock(dev);
      return retval;
    }

    partition = bd->bd_part;
    if (!partition){
      part_start   = 0;
      part_sectors = bd->bd_disk->capacity;
      part_end     = part_sectors - 1;
    } else {
      part_start   = partition->start_sect;
      part_end     = part_start + partition->nr_sects - 1;
      part_sectors = partition->nr_sects;
    }
    PDEBUG("part_start=0x%08x, part_end=0x%08x, part_sects=0x%08x",
	   part_start, part_end, part_sectors);
  }

  if(version == 4){
    unsigned long sys_area = 0;
    sys_area = dev->area[0].end + 1;
    start    = 0;
    end      = sys_area - 1;

    if(part_start > end || part_end < start){
      start   = 0;
      sectors = 0;
    } else {
      start   = (part_start>start)?part_start:start;
      end     = (end > part_end)?part_end:end;
      sectors = end - start + 1;
      start  -= part_start;
    }
    parm.p2_sys_start        = start;
    parm.p2_sys_sectors      = sectors;
    parm.p2_protect_start    = start;
    parm.p2_protect_sectors  = sectors;

    parm.p2_sys_RU_sectors   = dev->area[0].wsize;
    parm.p2_user_RU_sectors  = dev->area[1].wsize;
    parm.p2_AU_sectors       = spd_sdsta2au(b[163]);
    parm.p2_application_flag = 0xffff;
    spd_io_unlock(dev);
  } else if(version == 3){
    start = (b[37]<<24|b[38]<<16|b[39]<<8|b[40]);
    end   = (b[41]<<24|b[42]<<16|b[43]<<8|b[44]);

    if(part_start > end || part_end < start){
      start   = 0;
      sectors = 0;
    } else {
      start   = (part_start>start)?part_start:start;
      end     = (end > part_end)?part_end:end;
      sectors = end - start + 1;
      start  -= part_start;
    }
    parm.p2_sys_start   = start;
    parm.p2_sys_sectors = sectors;

    start = (b[65]<<24|b[66]<<16|b[67]<<8|b[68]);
    end   = (b[69]<<24|b[70]<<16|b[71]<<8|b[72]);
    if(part_start > end || part_end < start){
      start   = 0;
      sectors = 0;
    } else {
      start = (part_start>start)?part_start:start;
      end   = (end > part_end)?part_end:end;
      sectors = end - start + 1;
      start   -= part_start;
    }
    parm.p2_protect_start    = start;
    parm.p2_protect_sectors  = sectors;

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

  if(cmd == P2_GET_CARD_PARAMS){
    retval = copy_to_user((void *)arg, &parm, sizeof(parm));
    if(unlikely(retval < 0)){
      PERROR("<spd%c>copy_to_user() failed(%d)", dev->id+'a', retval);
      return retval;
    }
  } else {
    memcpy((void *)arg, &parm, sizeof(parm));
  }

  return 0;
}


static int ioc_bdev_card_initialize(spd_dev_t *dev, unsigned long arg)
{
  int retval;
  int version;
  PTRACE();

  version = (dev->spec_version>>4);
  if(version < 3){
    return -EIO;
  }

  retval = spd_io_lock(dev);
  if(retval < 0){
    return -EINTR;
  }

  dev->timeout = SPD_CMD_TIMEOUT;
  dev->retry   = SPD_NO_RETRY;
  retval = spd_card_initialize(dev);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>spd_card_initialize() failed(%d)", dev->id+'a', retval);
  }
  spd_io_unlock(dev);
  return 0;
}


static int ioc_bdev_identify_device(spd_dev_t *dev, unsigned long arg)
{
  int retval;
  PTRACE();

  retval = spd_io_lock(dev);
  if(retval < 0){
    return -EINTR;
  }

  dev->timeout = SPD_CMD_TIMEOUT;
  dev->retry   = SPD_NO_RETRY;
  retval = spd_identify_device(dev, dev->tmp_buf);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>spd_identify_device() failed(%d)", 'a'+dev->id, retval);
    spd_io_unlock(dev);
    return retval;
  }

  retval = copy_to_user((void *)arg, dev->tmp_buf, SPD_HARDSECT_SIZE);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>copy_to_user() failed(%d)", dev->id+'a', retval);
  }
  spd_io_unlock(dev);
  return retval;
}


static int ioc_bdev_card_terminate(spd_dev_t *dev, unsigned long arg)
{
  int retval;
  PTRACE();

  retval = spd_io_lock(dev);
  if(retval < 0){
    return -EINTR;
  }

  dev->timeout = SPD_CMD_LONG_TIMEOUT;
  dev->retry   = SPD_NO_RETRY;
  retval = spd_card_terminate(dev);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>spd_card_terminate() failed(%d)", dev->id+'a', retval);
  }
  spd_io_unlock(dev);
  return retval;
}


static int ioc_bdev_card_rescue(spd_dev_t *dev, unsigned long arg)
{
  int retval;
  int version;
  u8 option;
  PTRACE();

  version = dev->spec_version;
  if(version < 0x31 || 0x3F < version){ /* spec version from 3.1 to 3.15 */
    PERROR("<spd%c>Unsupported rescue mode before spec ver.3.1(%d.%d)",
	   dev->id+'a', dev->spec_version>>4, dev->spec_version&0x0F);
    return -EINVAL;
  }

  retval = copy_from_user(&option, (void *)arg, sizeof(option));
  if(unlikely(retval < 0)){
    PERROR("<spd%c>copy_from_user() failed(%d)", dev->id+'a', retval);
    return retval;
  }

  retval = spd_io_lock(dev);
  if(retval < 0){
    return -EINTR;
  }

  dev->timeout = SPD_CMD_TIMEOUT;
  dev->retry   = SPD_NO_RETRY;
  retval = spd_card_rescue(dev, option);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>spd_card_rescue() failed(%d)", dev->id+'a', retval);
  }
  spd_io_unlock(dev);
  return retval;
}


static int ioc_bdev_secured_rec(spd_dev_t *dev, unsigned long arg)
{
  int retval;
  int version;
  u8 ctrl;
  PTRACE();

  version = (dev->spec_version>>4);
  if(version != 4){
    PERROR("<spd%c>Unsupported Secured-REC before spec ver.4.0(%d.%d)",
	   dev->id+'a', dev->spec_version>>4, dev->spec_version&0x0F);
    return -EINVAL;
  }
  retval = copy_from_user(&ctrl, (void *)arg, sizeof(ctrl));
  if(unlikely(retval < 0)){
    PERROR("<spd%c>copy_from_user() failed(%d)", dev->id+'a', retval);
    return retval;
  }

  retval = spd_io_lock(dev);
  if(retval < 0){
    return -EINTR;
  }

  dev->timeout = SPD_CMD_TIMEOUT;
  dev->retry   = SPD_NO_RETRY;
  retval = spd_secured_rec(dev, ctrl);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>spd_secured_rec() failed(%d)", dev->id+'a', retval);
  }
  spd_io_unlock(dev);
  return retval;
}


static int ioc_bdev_start_rec(spd_dev_t *dev, unsigned long arg)
{
  int retval;
  int version;
  u8 id;
  PTRACE();

  version = (dev->spec_version>>4);
  if(version != 4){
    PERROR("<spd%c>Unsupported Start-REC before spec ver.4.0(%d.%d)",
	   dev->id+'a', dev->spec_version>>4, dev->spec_version&0x0F);
    return -EINVAL;
  }
  retval = copy_from_user(&id, (void *)arg, sizeof(id));
  if(unlikely(retval < 0)){
    PERROR("<spd%c>copy_from_user() failed(%d)", dev->id+'a', retval);
    return retval;
  }

  retval = spd_io_lock(dev);
  if(retval < 0){
    return -EINTR;
  }

  dev->timeout = SPD_CMD_TIMEOUT;
  dev->retry   = SPD_NO_RETRY;
  retval = spd_start_rec(dev, id);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>spd_start_rec() failed(%d)", dev->id+'a', retval);
  }
  spd_io_unlock(dev);
  return retval;
}


static int ioc_bdev_end_rec(spd_dev_t *dev, unsigned long arg)
{
  int retval;
  int version;
  u8 id;
  PTRACE();

  version = (dev->spec_version>>4);
  if(version != 4){
    PERROR("<spd%c>Unsupported End-REC before spec ver.4.0(%d.%d)",
	   dev->id+'a', dev->spec_version>>4, dev->spec_version&0x0F);
    return -EINVAL;
  }
  retval = copy_from_user(&id, (void *)arg, sizeof(id));
  if(unlikely(retval < 0)){
    PERROR("<spd%c>copy_from_user() failed(%d)", dev->id+'a', retval);
    return retval;
  }

  retval = spd_io_lock(dev);
  if(retval < 0){
    return -EINTR;
  }

  dev->timeout = SPD_CMD_TIMEOUT;
  dev->retry   = SPD_NO_RETRY;
  retval = spd_end_rec(dev, id);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>spd_end_rec() failed(%d)", dev->id+'a', retval);
  }
  spd_io_unlock(dev);
  return retval;
}


static int ioc_bdev_sd_device_reset(spd_dev_t *dev)
{
  int retval;
  int version;
  PTRACE();

  version = (dev->spec_version>>4);
  if(version != 4){
    PERROR("<spd%c>Unsupported SD device reset before spec ver.4.0(%d.%d)",
	   dev->id+'a', dev->spec_version>>4, dev->spec_version&0x0F);
    return -EINVAL;
  }
  retval = spd_io_lock(dev);
  if(retval < 0){
    return -EINTR;
  }

  dev->timeout = SPD_CMD_TIMEOUT;
  dev->retry   = SPD_NO_RETRY;
  retval = spd_sd_device_reset(dev);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>spd_sd_device_reset() failed(%d)", dev->id+'a', retval);
  }
  spd_io_unlock(dev);
  return retval;
}


static int ioc_bdev_table_recover(spd_dev_t *dev, unsigned long arg)
{
  int retval;
  int version;
  u8 ver;
  PTRACE();

  version = (dev->spec_version>>4);
  if(version != 4){
    PERROR("<spd%c>Unsupported table recover before spec ver.4.0(%d.%d)",
	   dev->id+'a', dev->spec_version>>4, dev->spec_version&0x0F);
    return -EINVAL;
  }
  retval = copy_from_user(&ver, (void *)arg, sizeof(ver));
  if(unlikely(retval < 0)){
    PERROR("<spd%c>copy_from_user() failed(%d)", dev->id+'a', retval);
    return retval;
  }

  retval = spd_io_lock(dev);
  if(retval < 0){
    return -EINTR;
  }

  dev->timeout = SPD_CMD_TIMEOUT;
  dev->retry   = SPD_NO_RETRY;
  retval = spd_table_recover(dev, ver);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>spd_table_recover() failed(%d)", dev->id+'a', retval);
  }
  spd_io_unlock(dev);
  return retval;
}


static int ioc_bdev_go_hibernate(spd_dev_t *dev)
{
  int retval;
  int version;
  PTRACE();

  version = (dev->spec_version>>4);
  if(version != 4){
    PERROR("<spd%c>Unsupported go hibernate before spec ver.4.0(%d.%d)",
	   dev->id+'a', dev->spec_version>>4, dev->spec_version&0x0F);
    return -EINVAL;
  }
  retval = spd_io_lock(dev);
  if(retval < 0){
    return -EINTR;
  }

  dev->timeout = SPD_CMD_TIMEOUT;
  dev->retry   = SPD_NO_RETRY;
  retval = spd_go_hibernate(dev);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>spd_go_hibernate() failed(%d)", dev->id+'a', retval);
  }
  spd_io_unlock(dev);
  return retval;
}


static int ioc_bdev_lmg(spd_dev_t *dev, unsigned long arg)
{
  int retval;
  int version;
  struct p2_sdcmd_d_arg parm;
  PTRACE();

  version = (dev->spec_version>>4);
  if(version != 4){
    PERROR("<spd%c>Unsupported LMG before spec ver.4.0(%d.%d)",
	   dev->id+'a', dev->spec_version>>4, dev->spec_version&0x0F);
    return -EINVAL;
  }
  retval = copy_from_user(&parm, (void *)arg, sizeof(parm));
  if(unlikely(retval < 0)){
    PERROR("<spd%c>copy_from_user() failed(%d)", dev->id+'a', retval);
    return retval;
  }

  retval = spd_io_lock(dev);
  if(retval < 0){
    return -EINTR;
  }

  dev->timeout = SPD_CMD_TIMEOUT;
  dev->retry   = SPD_NO_RETRY;
  retval = spd_lmg(dev, parm.sd_arg, (u8 *)parm.buf);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>spd_lmg() failed(%d)", dev->id+'a', retval);
  }
  spd_io_unlock(dev);
  return retval;
}


static int ioc_bdev_get_sst(spd_dev_t *dev, unsigned long arg)
{
  int retval;
  int version;
  struct p2_sdcmd_d_arg parm;
  PTRACE();

  version = (dev->spec_version>>4);
  if(version != 4){
    PERROR("<spd%c>Unsupported Get SST before spec ver.4.0(%d.%d)",
	   dev->id+'a', dev->spec_version>>4, dev->spec_version&0x0F);
    return -EINVAL;
  }
  retval = copy_from_user(&parm, (void *)arg, sizeof(parm));
  if(unlikely(retval < 0)){
    PERROR("<spd%c>copy_from_user() failed(%d)", dev->id+'a', retval);
    return retval;
  }

  retval = spd_io_lock(dev);
  if(retval < 0){
    return -EINTR;
  }

  dev->timeout = SPD_CMD_TIMEOUT;
  dev->retry   = SPD_NO_RETRY;
  retval = spd_get_sst(dev, (u8 *)dev->tmp_buf);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>spd_get_sst() failed(%d)", dev->id+'a', retval);
    spd_io_unlock(dev);
    return retval;
  }

  retval = copy_to_user((void *)parm.buf, dev->tmp_buf, SPD_HARDSECT_SIZE);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>copy_to_user() failed(%d)", dev->id+'a', retval);
  }
  spd_io_unlock(dev);
  return retval;
}


static int ioc_bdev_get_dinfo(spd_dev_t *dev, unsigned long arg)
{
  int retval;
  int version;
  struct p2_sdcmd_d_arg parm;
  PTRACE();

  version = (dev->spec_version>>4);
  if(version != 4){
    PERROR("<spd%c>Unsupported Get DINFO before spec ver.4.0(%d.%d)",
	   dev->id+'a', dev->spec_version>>4, dev->spec_version&0x0F);
    return -EINVAL;
  }
  retval = copy_from_user(&parm, (void *)arg, sizeof(parm));
  if(unlikely(retval < 0)){
    PERROR("<spd%c>copy_from_user() failed(%d)", dev->id+'a', retval);
    return retval;
  }

  retval = spd_io_lock(dev);
  if(retval < 0){
    return -EINTR;
  }

  dev->timeout = SPD_CMD_TIMEOUT;
  dev->retry   = SPD_NO_RETRY;
  retval = spd_get_dinfo(dev, parm.sd_arg, (u8 *)dev->tmp_buf);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>spd_get_dinfo() failed(%d)", dev->id+'a', retval);
    spd_io_unlock(dev);
    return retval;
  }

  retval = copy_to_user((void *)parm.buf, dev->tmp_buf, SPD_HARDSECT_SIZE);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>copy_to_user() failed(%d)", dev->id+'a', retval);
  }
  spd_io_unlock(dev);
  return retval;
}


static int ioc_bdev_dmg(spd_dev_t *dev, unsigned long arg)
{
  int retval;
  int version;
  struct p2_sdcmd_d_arg parm;
  PTRACE();

  version = (dev->spec_version>>4);
  if(version != 4){
    PERROR("<spd%c>Unsupported DMG before spec ver.4.0(%d.%d)",
	   dev->id+'a', dev->spec_version>>4, dev->spec_version&0x0F);
    return -EINVAL;
  }
  retval = copy_from_user(&parm, (void *)arg, sizeof(parm));
  if(unlikely(retval < 0)){
    PERROR("<spd%c>copy_from_user() failed(%d)", dev->id+'a', retval);
    return retval;
  }

  retval = spd_io_lock(dev);
  if(retval < 0){
    return -EINTR;
  }

  dev->timeout = SPD_CMD_TIMEOUT;
  dev->retry   = SPD_NO_RETRY;
  retval = spd_dmg(dev, parm.sd_arg, (u8 *)parm.buf);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>spd_dmg() failed(%d)", dev->id+'a', retval);
  }
  spd_io_unlock(dev);
  return retval;
}


static int ioc_bdev_esw(spd_dev_t *dev, unsigned long arg)
{
  int retval;
  int version;
  u8 sw;
  PTRACE();

  version = (dev->spec_version>>4);
  if(version != 4){
    PERROR("<spd%c>Unsupported ESW before spec ver.4.0(%d.%d)",
	   dev->id+'a', dev->spec_version>>4, dev->spec_version&0x0F);
    return -EINVAL;
  }
  retval = copy_from_user(&sw, (void *)arg, sizeof(sw));
  if(unlikely(retval < 0)){
    PERROR("<spd%c>copy_from_user() failed(%d)", dev->id+'a', retval);
    return retval;
  }

  retval = spd_io_lock(dev);
  if(retval < 0){
    return -EINTR;
  }

  dev->timeout = SPD_CMD_TIMEOUT;
  dev->retry   = SPD_NO_RETRY;
  retval = spd_esw(dev, sw);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>spd_esw() failed(%d)", dev->id+'a', retval);
  }
  spd_io_unlock(dev);
  return retval;
}


static int ioc_bdev_get_ph(spd_dev_t *dev, unsigned long arg)
{
  int retval;
  int version;
  struct p2_sdcmd_d_arg parm;
  PTRACE();

  version = (dev->spec_version>>4);
  if(version != 4){
    PERROR("<spd%c>Unsupported Get PH before spec ver.4.0(%d.%d)",
	   dev->id+'a', dev->spec_version>>4, dev->spec_version&0x0F);
    return -EINVAL;
  }
  retval = copy_from_user(&parm, (void *)arg, sizeof(parm));
  if(unlikely(retval < 0)){
    PERROR("<spd%c>copy_from_user() failed(%d)", dev->id+'a', retval);
    return retval;
  }

  retval = spd_io_lock(dev);
  if(retval < 0){
    return -EINTR;
  }

  dev->timeout = SPD_CMD_TIMEOUT;
  dev->retry   = SPD_NO_RETRY;
  retval = spd_get_ph(dev, parm.sd_arg, (u8 *)dev->tmp_buf);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>spd_get_ph() failed(%d)", dev->id+'a', retval);
    spd_io_unlock(dev);
    return retval;
  }

  retval = copy_to_user((void *)parm.buf, dev->tmp_buf, SPD_HARDSECT_SIZE);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>copy_to_user() failed(%d)", dev->id+'a', retval);
  }
  spd_io_unlock(dev);
  return retval;
}


static int ioc_bdev_get_linfo(spd_dev_t *dev, unsigned long arg)
{
  int retval;
  int version;
  struct p2_sdcmd_d_arg parm;
  PTRACE();

  version = (dev->spec_version>>4);
  if(version != 4){
    PERROR("<spd%c>Unsupported Get LINFO before spec ver.4.0(%d.%d)",
	   dev->id+'a', dev->spec_version>>4, dev->spec_version&0x0F);
    return -EINVAL;
  }
  retval = copy_from_user(&parm, (void *)arg, sizeof(parm));
  if(unlikely(retval < 0)){
    PERROR("<spd%c>copy_from_user() failed(%d)", dev->id+'a', retval);
    return retval;
  }

  retval = spd_io_lock(dev);
  if(retval < 0){
    return -EINTR;
  }

  dev->timeout = SPD_CMD_TIMEOUT;
  dev->retry   = SPD_NO_RETRY;
  retval = spd_get_linfo(dev, parm.sd_arg, (u8 *)dev->tmp_buf);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>spd_get_linfo() failed(%d)", dev->id+'a', retval);
    spd_io_unlock(dev);
    return retval;
  }

  retval = copy_to_user((void *)parm.buf, dev->tmp_buf, SPD_HARDSECT_SIZE);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>copy_to_user() failed(%d)", dev->id+'a', retval);
  }
  spd_io_unlock(dev);
  return retval;
}


static int ioc_bdev_dau(spd_dev_t *dev, unsigned long arg)
{
  int retval;
  int version;
  struct p2_sdcmd_d_arg parm;
  PTRACE();

  version = (dev->spec_version>>4);
  if(version != 4){
    PERROR("<spd%c>Unsupported DAU before spec ver.4.0(%d.%d)",
	   dev->id+'a', dev->spec_version>>4, dev->spec_version&0x0F);
    return -EINVAL;
  }
  retval = copy_from_user(&parm, (void *)arg, sizeof(parm));
  if(unlikely(retval < 0)){
    PERROR("<spd%c>copy_from_user() failed(%d)", dev->id+'a', retval);
    return retval;
  }

  retval = spd_io_lock(dev);
  if(retval < 0){
    return -EINTR;
  }

  dev->timeout = SPD_CMD_TIMEOUT;
  dev->retry   = SPD_NO_RETRY;
  retval = spd_dau(dev, parm.sd_arg, (u8 *)parm.buf);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>spd_dau() failed(%d)", dev->id+'a', retval);
  }
  spd_io_unlock(dev);
  return retval;
}


static int ioc_bdev_set_dparam(spd_dev_t *dev, unsigned long arg)
{
  int retval;
  int version;
  struct p2_sdcmd_d_arg parm;
  PTRACE();

  version = (dev->spec_version>>4);
  if(version != 4){
    PERROR("<spd%c>Unsupported SET DPARAM before spec ver.4.0(%d.%d)",
	   dev->id+'a', dev->spec_version>>4, dev->spec_version&0x0F);
    return -EINVAL;
  }
  retval = copy_from_user(&parm, (void *)arg, sizeof(parm));
  if(unlikely(retval < 0)){
    PERROR("<spd%c>copy_from_user() failed(%d)", dev->id+'a', retval);
    return retval;
  }

  retval = spd_io_lock(dev);
  if(retval < 0){
    return -EINTR;
  }

  dev->timeout = SPD_CMD_TIMEOUT;
  dev->retry   = SPD_NO_RETRY;
  retval = spd_set_dparam(dev, parm.sd_arg, (u8 *)parm.buf);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>spd_dparam() failed(%d)", dev->id+'a', retval);
  }
  spd_io_unlock(dev);
  return retval;
}


static int ioc_bdev_get_phs(spd_dev_t *dev, unsigned long arg)
{
  int retval;
  int version;
  struct p2_sdcmd_d_arg parm;
  PTRACE();

  version = (dev->spec_version>>4);
  if(version != 4){
    PERROR("<spd%c>Unsupported Get PHS before spec ver.4.0(%d.%d)",
	   dev->id+'a', dev->spec_version>>4, dev->spec_version&0x0F);
    return -EINVAL;
  }
  retval = copy_from_user(&parm, (void *)arg, sizeof(parm));
  if(unlikely(retval < 0)){
    PERROR("<spd%c>copy_from_user() failed(%d)", dev->id+'a', retval);
    return retval;
  }

  retval = spd_io_lock(dev);
  if(retval < 0){
    return -EINTR;
  }

  dev->timeout = SPD_CMD_TIMEOUT;
  dev->retry   = SPD_NO_RETRY;
  retval = spd_get_phs(dev, parm.sd_arg, (u8 *)dev->tmp_buf);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>spd_get_phs() failed(%d)", dev->id+'a', retval);
    spd_io_unlock(dev);
    return retval;
  }

  retval = copy_to_user((void *)parm.buf, dev->tmp_buf, SPD_HARDSECT_SIZE);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>copy_to_user() failed(%d)", dev->id+'a', retval);
  }
  spd_io_unlock(dev);
  return retval;
}


static int ioc_bdev_set_aut_status(spd_dev_t *dev, unsigned long arg)
{
  int retval = 0;
  unsigned long status = 0;
  PTRACE();

  retval = copy_from_user(&status, (void *)arg, sizeof(status));
  if(unlikely(retval < 0)){
    PERROR("<spd%c>copy_from_user() failed(%d)", dev->id+'a', retval);
    return retval;
  }

  if(status&SPD_AUT_STATUS_ERROR){
    PINFO("<spd%c>set SPD_ADPT_AUT_ERROR", dev->id+'a');
    spd_set_AUE(dev);
  }

  return 0;
}
