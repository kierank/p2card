/*
 P2 card driver adaptor modules
 $Id:$
*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/ioctl.h>
#include <linux/errno.h>

#include "spd.h"
#include "spd_bdev.h"

#line __LINE__ "spd_adpt.c" /* Replace full path(__FILE__) to spd_adpt.c. */

static void spd_adpt_write_complete_handler(spd_dev_t *dev);
static void spd_adpt_fsmiw_complete_handler(spd_dev_t *dev);
static int  spd_adpt_make_rmw_sg(spd_dev_t *dev);
static void spd_adpt_rmw_read_complete_handler(spd_dev_t *dev);
static void spd_adpt_rmw_write_complete_handler(spd_dev_t *dev);


int spd_adpt_fsmi_write(spd_dev_t *dev, unsigned long arg)
{
  struct p2_directw_entry *entry = NULL;
  int list_size = sizeof(struct p2_directw_list);
  int sg_size = sizeof(spd_scatterlist_t);
  int entry_size = 0;
  int retval = 0;
  int pos = 0;
  PTRACE();

  dev->directw_list = (struct p2_directw_list *)kmalloc(list_size, GFP_KERNEL);
  if(unlikely(NULL == dev->directw_list)){
    PERROR("<spdr%c>kmalloc failed", dev->id+'a');
    retval = -ENOMEM;
    goto ABORT;
  }

  retval = copy_from_user(dev->directw_list, (void *)arg, list_size);
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>copy_from_user() failed(%d)", dev->id+'a', retval);
    retval = -EFAULT;
    dev->directw_list->entry = NULL;
    goto ABORT;
  }

  entry_size = dev->directw_list->num * sizeof(struct p2_directw_entry);
  dev->directw_list->entry = (struct p2_directw_entry *)kmalloc(entry_size, GFP_KERNEL);
  if(unlikely(NULL == dev->directw_list->entry)){
    PERROR("<spdr%c>kmalloc failed", dev->id+'a');
    retval = -ENOMEM;
    goto ABORT;
  }

  retval = copy_from_user(dev->directw_list->entry, (void *)((struct p2_directw_list *)arg)->entry, entry_size);
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>copy_from_user() failed(%d)", dev->id+'a', retval);
    retval = -EFAULT;
    dev->directw_list->entry = NULL;
    goto ABORT;
  }

  entry = dev->directw_list->entry;
  dev->directw_list->pos = 0;
  pos = dev->directw_list->pos;

  if(unlikely(NULL == entry)){
    PERROR("<spdr%c>FSMI write arg is NULL!", dev->id+'a');
    retval = -EINVAL;
    goto ABORT;
  }
  dev->sg[0].bus_address = entry[pos].sg_bus_address;
  dev->sg[0].count       = entry[pos].sg_entry_size;

  PINFO("<spdr%c>entry[%d].sector=x0x%08lx count=0x%08x", dev->id+'a', pos, entry[pos].sector, entry[pos].count);
  PINFO("<spdr%c>sg[0].addr=0x%08x count=0x%08x", dev->id+'a', dev->sg[0].bus_address, dev->sg[0].count);
  spd_dma_cache_wback(dev->dev, dev->sg, sg_size);
  dev->sg_n_entry = 1;
  spd_dma_cache_wback(dev->dev,
		      bus_to_virt(le32_to_cpu((u32)dev->sg[0].bus_address)),
		      le32_to_cpu(dev->sg[0].count)&(~SPD_SG_ENDMARK));

  dev->retry            = SPD_NO_RETRY;
  dev->timeout          = dev->dma_timeout;
  dev->complete_handler = spd_adpt_fsmiw_complete_handler;

  PRINT_SG(dev->sg, dev->sg_n_entry, dev->id);

  retval = spd_seq_write_sector(dev, entry[pos].sector, entry[pos].count, dev->sg);
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>spd_seq_write_sector() failed(%d)", dev->id+'a', retval);
    goto ABORT;
  }

  return 0;

 ABORT:
  if(dev->directw_list){
    if(dev->directw_list->entry){
      kfree(dev->directw_list->entry);
      dev->directw_list->entry = NULL;
    }
    kfree(dev->directw_list);
    dev->directw_list = NULL;
  }

  spd_send_event(dev, SPD_EVENT_DIRECT_COMPLETE, retval);
  dev->complete_handler = NULL;
  spd_io_unlock(dev);

  return retval;
}


static void spd_adpt_fsmiw_complete_handler(spd_dev_t *dev)
{
  struct p2_directw_entry *entry = NULL;
  int retval = 0;
  int sg_size = sizeof(spd_scatterlist_t);
  int pos = 0;
  PTRACE();

  if(dev->errcode){
    goto EXIT;
  }

  dev->directw_list->pos++;
  pos = dev->directw_list->pos;
  if(pos >= dev->directw_list->num){
    PINFO("<spdr%c>FSMI write list finished(%d %d)",
	  dev->id+'a', pos, dev->directw_list->num);
    goto EXIT;
  }

  entry = dev->directw_list->entry;
  if(unlikely(NULL == entry)){
    PERROR("<spdr%c>FSMI write entry is NULL!", dev->id+'a');
    dev->errcode = -EINVAL;
    goto EXIT;
  }

  PINFO("<spdr%c>entry[%d].sector=0x%08lx count=0x%08x", dev->id+'a', pos, entry[pos].sector, entry[pos].count);

  dev->sg[0].bus_address = entry[pos].sg_bus_address;
  dev->sg[0].count       = entry[pos].sg_entry_size;
  PINFO("<spdr%c>sg[0].addr=0x%08x count=0x%08x", dev->id+'a', dev->sg[0].bus_address, dev->sg[0].count );

  spd_dma_cache_wback(dev->dev, dev->sg, sg_size);
  dev->sg_n_entry = 1;
  spd_dma_cache_wback(dev->dev,
		      bus_to_virt(le32_to_cpu((u32)dev->sg[0].bus_address)),
		      le32_to_cpu(dev->sg[0].count)&(~SPD_SG_ENDMARK));

  dev->retry            = SPD_NO_RETRY;
  dev->timeout          = dev->dma_timeout;
  dev->complete_handler = spd_adpt_fsmiw_complete_handler;

  PRINT_SG(dev->sg, dev->sg_n_entry, dev->id);

  retval = spd_seq_write_sector(dev, entry[pos].sector, entry[pos].count, dev->sg);
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>spd_seq_write_sector() failed(%d)", dev->id+'a', retval);
    dev->errcode = retval;
    goto EXIT;
  }

  return;

 EXIT:
  if(dev->directw_list){
    if(dev->directw_list->entry){
      kfree(dev->directw_list->entry);
      dev->directw_list->entry = NULL;
    }
    kfree(dev->directw_list);
    dev->directw_list = NULL;
  }

  spd_send_event(dev, SPD_EVENT_DIRECT_COMPLETE, dev->errcode);
  dev->complete_handler = NULL;
  spd_io_unlock(dev);
}


int spd_adpt_sdcmd_write(spd_dev_t *dev, unsigned long arg)
{
  struct p2_sdcmd_w_arg sdcmd;
  struct p2_direct_arg *entry;
  int retval = 0;
  int i = 0;
  PTRACE();

  retval = copy_from_user(&sdcmd, (void *)arg, sizeof(sdcmd));
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>copy_from_user() failed(%d)", dev->id+'a', retval);
    retval = -EFAULT;
    goto ABORT;
  }

  entry = sdcmd.arg;
  if(unlikely(NULL == entry)){
    PERROR("<spdr%c>Write params is NULL!", dev->id+'a');
    retval = -EINVAL;
    goto ABORT;
  }

  if(entry->sg_table_size > SPD_SG_SIZE){
    PERROR("<spdr%c>invalid sg table size(%x)", dev->id+'a', entry->sg_table_size);
    retval = -EINVAL;
    goto ABORT;
  }

  dev->retry = SPD_NO_RETRY;

  switch(sdcmd.cmd){
  case P2_SDCMD_CREATE_DIR:
    {
      retval = spd_create_dir(dev, sdcmd.id);
      if(unlikely(retval < 0)){
	PERROR("<spdr%c>spd_create_dir failed(%d)", dev->id+'a', retval);
	goto ABORT;
      }
      break;
    }

  case P2_SDCMD_UPDATE_CI:
    {
      retval = spd_update_ci(dev, sdcmd.id);
      if(unlikely(retval < 0)){
	PERROR("<spdr%c>spd_update_ci failed(%d)", dev->id+'a', retval);
	goto ABORT;
      }
      break;
    }

  default:
    {
      PERROR("<spdr%c>unknown SDCMD(%d)", dev->id+'a', sdcmd.cmd);
      retval = -EINVAL;
      goto ABORT;
    }
  }

  retval  = copy_from_user(dev->sg, entry->sg_table, entry->sg_table_size);
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>copy_from_user() failed(%d)", dev->id+'a', retval);
    goto ABORT;
  }

  spd_dma_cache_wback(dev->dev, dev->sg, entry->sg_table_size);
  dev->sg_n_entry = entry->sg_table_size/sizeof(spd_scatterlist_t);
  for(i = 0; i < dev->sg_n_entry; i++){
    spd_dma_cache_wback(dev->dev,
			bus_to_virt(le32_to_cpu((u32)dev->sg[i].bus_address)),
			le32_to_cpu(dev->sg[i].count)&(~SPD_SG_ENDMARK));
  }

  dev->timeout          = dev->dma_timeout;
  dev->complete_handler = spd_adpt_write_complete_handler;

  PRINT_SG(dev->sg, dev->sg_n_entry, dev->id);

  retval = spd_seq_write_sector(dev, entry->sector, entry->count, dev->sg);
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>spd_seq_write_sector() failed(%d)", dev->id+'a', retval);
    goto ABORT;
  }

  return 0;

 ABORT:
  spd_send_event(dev, SPD_EVENT_DIRECT_COMPLETE, retval);
  dev->complete_handler = NULL;
  spd_io_unlock(dev);

  return retval;
}


static void spd_adpt_write_complete_handler(spd_dev_t *dev)
{
  PTRACE();

  PINFO("<spdr%c>complete time=%dms errcode=%d",
	dev->id+'a', jiffies_to_msecs(dev->ticks), dev->errcode);

  spd_send_event(dev, SPD_EVENT_DIRECT_COMPLETE, dev->errcode);
  dev->complete_handler = NULL;
  spd_io_unlock(dev);
}


int spd_adpt_read_modify_write(spd_dev_t *dev, unsigned long arg)
{
  struct p2_directw_entry *entry = NULL;
  int list_size = sizeof(struct p2_directw_list);
  int entry_size = 0;
  int retval = 0;
  int pos = 0;
  int i = 0;
  u32 sector = 0;
  int wsize = 0;
  int major = dev->spec_version >> 4;
  PTRACE();

  if(dev->cache == NULL){
    dev->cache = spd_cache_alloc(dev);
  }

  if(dev->cache == NULL){
    PERROR("<spdr%c>cache alloc failed", dev->id+'a');
    retval = -EBUSY;
    goto ABORT;
  }

  dev->directw_list = (struct p2_directw_list *)kmalloc(list_size, GFP_KERNEL);
  if(unlikely(NULL == dev->directw_list)){
    PERROR("<spdr%c>kmalloc failed", dev->id+'a');
    retval = -ENOMEM;
    goto ABORT;
  }

  retval = copy_from_user(dev->directw_list, (void *)arg, list_size);
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>copy_from_user() failed(%d)", dev->id+'a', retval);
    retval = -EFAULT;
    dev->directw_list->entry = NULL;
    goto ABORT;
  }

  entry_size = dev->directw_list->num * sizeof(struct p2_directw_entry);
  dev->directw_list->entry = (struct p2_directw_entry *)kmalloc(entry_size, GFP_KERNEL);
  if(unlikely(NULL == dev->directw_list->entry)){
    PERROR("<spdr%c>kmalloc failed", dev->id+'a');
    retval = -ENOMEM;
    goto ABORT;
  }

  retval = copy_from_user(dev->directw_list->entry, (void *)((struct p2_directw_list *)arg)->entry, entry_size);
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>copy_from_user() failed(%d)", dev->id+'a', retval);
    retval = -EFAULT;
    goto ABORT;
  }

  entry = dev->directw_list->entry;
  if(unlikely(NULL == entry)){
    PERROR("<spdr%c>DirectRMW arg is NULL!", dev->id+'a');
    retval = -EINVAL;
    goto ABORT;
  }
  dev->directw_list->pos = 0;
  pos = dev->directw_list->pos;

  for(i = 0; i < dev->directw_list->num; i++){
    spd_dma_cache_wback(dev->dev,
			bus_to_virt(le32_to_cpu((u32)entry[i].sg_bus_address)),
			le32_to_cpu(entry[i].sg_entry_size));
  }

  dev->bdev->hard_nr_sectors = entry[pos].count;
  dev->bdev->rmw_sector      = entry[pos].sector;
  dev->bdev->rmw_count       = entry[pos].count;
  dev->bdev->rmw_sg_offset   = 0;

  dev->retry   = (major == 4) ? SPD_NO_RETRY : dev->dma_retry;
  dev->timeout = dev->dma_timeout;

  if(spd_cache_is_dirty(dev->cache)){
    dev->complete_handler = spd_adpt_rmw_write_complete_handler;
    spd_cache_prepare(dev, SPD_DIR_WRITE);
    retval = spd_seq_write_sector(dev,
				  dev->cache->sector,
				  dev->cache->n_sector,
				  dev->cache->sg);
    if(unlikely(retval < 0)){
      PERROR("<spdr%c>spd_seq_write_sector() failed(%d)", dev->id+'a', retval);
      goto ABORT;
    }

    return retval;
  }

  sector = dev->bdev->rmw_sector;
  wsize  = spd_get_wsize(dev, sector);
  if(unlikely(wsize < 0)){
    PERROR("<spdr%c>spd_get_wsize() failed sector=%08x", dev->id+'a', sector);
    retval = -EINVAL;
    goto ABORT;
  }
  sector = align_sector(sector, wsize, spd_get_sector_offset(dev, sector));

  if(sector == dev->bdev->rmw_sector && dev->bdev->rmw_count >= wsize){
    dev->cache->sector   = sector;
    dev->cache->n_sector = wsize;
    retval = spd_adpt_make_rmw_sg(dev);

    if(unlikely(retval < 0)){
      PERROR("<spdr%c>spd_adpt_make_rmw_sg() failed(%d)", dev->id+'a', retval);
      goto ABORT;
    }

    dev->complete_handler = spd_adpt_rmw_write_complete_handler;
    retval = spd_seq_write_sector(dev, sector, wsize, dev->sg);
    if(unlikely(retval < 0)){
      PERROR("<spdr%c>spd_seq_write_sector() failed(%d)", dev->id+'a', retval);
      goto ABORT;
    }

    dev->cache->sector = (u32)-1;
    spd_cache_clr_dirty(dev->cache);

    return 0;
  }

  dev->cache->sector   = sector;
  dev->cache->n_sector = wsize;

  dev->complete_handler = spd_adpt_rmw_read_complete_handler;
  spd_cache_prepare(dev, SPD_DIR_READ);
  retval = spd_read_sector(dev, 
                           dev->cache->sector,
                           dev->cache->n_sector,
                           dev->cache->sg);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>spd_read_sector() failed(%d)", dev->id+'a', retval);
    goto ABORT;
  }

  return 0;

 ABORT:
  PINFO("<spdr%c>ABORT at %s", dev->id+'a', __FUNCTION__);
  if(dev->cache){
    dev->cache->sector = (u32)-1;
    spd_cache_clr_dirty(dev->cache);
  }

  if(dev->directw_list){
    if(dev->directw_list->entry){
      kfree(dev->directw_list->entry);
      dev->directw_list->entry = NULL;
    }
    kfree(dev->directw_list);
    dev->directw_list = NULL;
  }

  spd_send_event(dev, SPD_EVENT_DIRECT_COMPLETE, retval);
  dev->complete_handler = NULL;
  spd_io_unlock(dev);

  return retval;
}


static void spd_adpt_rmw_read_complete_handler(spd_dev_t *dev)
{
  int retval;
  PTRACE();

  PINFO("<spdr%c>complete time=%dms errcode=%d",
        dev->id+'a', jiffies_to_msecs(dev->ticks), dev->errcode);

  if(unlikely(dev->errcode < 0)){
    retval = dev->errcode;
    goto ABORT;
  }

  retval = spd_adpt_make_rmw_sg(dev);
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>spd_adpt_make_rmw_sg() failed(%d)", dev->id+'a', retval);
    goto ABORT;
  }

  dev->complete_handler = spd_adpt_rmw_write_complete_handler;
  retval = spd_seq_write_sector(dev, 
				dev->cache->sector,
				dev->cache->n_sector,
				dev->sg);
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>spd_seq_write_sector() failed(%d)", dev->id+'a', retval);
    goto ABORT;
  }

  dev->cache->sector = (u32)-1;
  spd_cache_clr_dirty(dev->cache);

  return;

ABORT:
  PINFO("<spdr%c>ABORT at %s", dev->id+'a', __FUNCTION__);
  if(dev->cache){
    dev->cache->sector = (u32)-1;
    spd_cache_clr_dirty(dev->cache);
  }

  if(dev->directw_list){
    if(dev->directw_list->entry){
      kfree(dev->directw_list->entry);
      dev->directw_list->entry = NULL;
    }
    kfree(dev->directw_list);
    dev->directw_list = NULL;
  }

  spd_send_event(dev, SPD_EVENT_DIRECT_COMPLETE, retval);
  dev->complete_handler = NULL;
  spd_io_unlock(dev);
}


static void spd_adpt_rmw_write_complete_handler(spd_dev_t *dev)
{
  int retval = 0;
  u32 sector = 0;
  int wsize = 0;
  PTRACE();

  PINFO("<spdr%c>complete time=%dms errcode=%d",
	dev->id+'a', jiffies_to_msecs(dev->ticks), dev->errcode);

  if(dev->cache){
    spd_cache_clr_dirty(dev->cache);
  }

  if(unlikely(dev->errcode < 0)){
    retval = dev->errcode;
    goto ABORT;
  }

  if((dev->bdev->rmw_count == 0)
     && (dev->directw_list->pos == dev->directw_list->num)){
    if(dev->directw_list){
      if(dev->directw_list->entry){
	kfree(dev->directw_list->entry);
	dev->directw_list->entry = NULL;
      }
      kfree(dev->directw_list);
      dev->directw_list = NULL;
    }

    spd_adpt_write_complete_handler(dev);
    return;
  }

  sector = dev->bdev->rmw_sector;
  wsize  = spd_get_wsize(dev, sector);
  if(unlikely(wsize < 0)){
    PERROR("<spdr%c>spd_get_wsize() failed sector=%08x", dev->id+'a', sector);
    retval = -EINVAL;
    goto ABORT;
  }
  sector = align_sector(sector, wsize, spd_get_sector_offset(dev, sector));

  if(sector == dev->bdev->rmw_sector && dev->bdev->rmw_count >= wsize){
    dev->cache->sector   = sector;
    dev->cache->n_sector = wsize;
    retval = spd_adpt_make_rmw_sg(dev);
    if(unlikely(retval < 0)){
      PERROR("<spdr%c>spd_adpt_make_rmw_sg() failed(%d)", dev->id+'a', retval);
      goto ABORT;
    }

    dev->complete_handler = spd_adpt_rmw_write_complete_handler;
    retval = spd_seq_write_sector(dev, sector, wsize, dev->sg);
    if(unlikely(retval < 0)){
      PERROR("<spdr%c>spd_seq_write_sector() failed(%d)", dev->id+'a', retval);
      goto ABORT;
    }

    dev->cache->sector = (u32)-1;
    spd_cache_clr_dirty(dev->cache);

    return;
  }

  dev->cache->sector   = sector;
  dev->cache->n_sector = wsize;

  dev->complete_handler = spd_adpt_rmw_read_complete_handler;
  spd_cache_prepare(dev, SPD_DIR_READ);
  retval = spd_read_sector(dev,
                           dev->cache->sector,
                           dev->cache->n_sector,
                           dev->cache->sg);
  if(unlikely(retval < 0)){
    PERROR("<spdr%c>spd_read_sector() failed(%d)", dev->id+'a', retval);
    goto ABORT;
  }

  return;

ABORT:
  PINFO("<spdr%c>ABORT at %s", dev->id+'a', __FUNCTION__);
  if(dev->cache){
    dev->cache->sector = (u32)-1;
    spd_cache_clr_dirty(dev->cache);
  }

  if(dev->directw_list){
    if(dev->directw_list->entry){
      kfree(dev->directw_list->entry);
      dev->directw_list->entry = NULL;
    }
    kfree(dev->directw_list);
    dev->directw_list = NULL;
  }

  spd_send_event(dev, SPD_EVENT_DIRECT_COMPLETE, retval);
  dev->complete_handler = NULL;
  spd_io_unlock(dev);
}


static int spd_adpt_make_rmw_sg(spd_dev_t *dev)
{
  spd_scatterlist_t *sg = dev->sg;
  struct p2_directw_entry *entry = NULL;
  int n = 0, i = 0, pos = 0;
  int len1 = 0, len2 = 0, len3 = 0;
  u8  *virt_addr = NULL;
  u32 b_size = 0, size = 0, offset = 0, b_offset = 0;
  u32 c_size = 0;
  u32 cur_sector = 0;
  int endflag = 0;
  PTRACE();

  entry = dev->directw_list->entry;
  if(unlikely(NULL == entry)){
    PERROR("<spdr%c>DirectRMW entry is NULL!", dev->id+'a');
    return -EINVAL;
  }

  PINFO("<spdr%c>cache_sector=%08x, rmw_sector=%08x, rmw_count=%04x",
        dev->id+'a',
	dev->cache->sector, 
        dev->bdev->rmw_sector,
        dev->bdev->rmw_count);

  PCOMMAND("<spdr%c>USE_CACHE id=%02d, cache_sector=%08x, rmw_sector=%08x, rmw_count=%04x",
           dev->id+'a',
           dev->cache->id,
           dev->cache->sector,
           dev->bdev->rmw_sector,
           dev->bdev->rmw_count);

  cur_sector = dev->cache->sector;
  for(pos = dev->directw_list->pos; pos < dev->directw_list->num; pos++){

    len1 = dev->bdev->rmw_sector - cur_sector;

    if(entry[pos].sector >= dev->cache->sector+dev->cache->n_sector){
      len1 = 0;
      len2 = 0;
      len3 = dev->cache->sector+dev->cache->n_sector - cur_sector;

      dev->bdev->rmw_count   = entry[pos].count;
      dev->bdev->rmw_sector  = entry[pos].sector;
      dev->directw_list->pos = pos;
      endflag = 1;
    } else if(len1 + entry[pos].count > dev->cache->n_sector){
      len2 = dev->cache->n_sector - len1;
      len3 = 0;

      dev->bdev->rmw_count  -= len2;
      dev->bdev->rmw_sector  = dev->cache->sector + dev->cache->n_sector;
      dev->directw_list->pos = pos;
      endflag = 1;
    } else if(pos+1 == dev->directw_list->num){
      len2 = dev->bdev->rmw_count;
      len3 = (dev->cache->sector+dev->cache->n_sector - cur_sector) - (len1+len2);

      dev->bdev->rmw_count   = 0;
      dev->bdev->rmw_sector  = (u32)-1;
      dev->directw_list->pos = dev->directw_list->num;
      endflag = 1;
    } else {
      len2 = dev->bdev->rmw_count;
      len3 = 0;

      dev->bdev->rmw_count   = entry[pos+1].count;
      dev->bdev->rmw_sector  = entry[pos+1].sector;
      dev->directw_list->pos = pos;
      endflag = 0;
    }

    PINFO("<spdr%c>pos=%d cur=%08x n=%d len1=%d, len2=%d, len3=%d",
	  dev->id+'a', pos, cur_sector, n, len1, len2, len3);

    PINFO("<spdr%c>dev->pos=%d rmw_sector=%08x rmw_count=%d, flag=%d", dev->id+'a',
	  dev->directw_list->pos, dev->bdev->rmw_sector, dev->bdev->rmw_count, endflag);

    if(len1 < 0 || len2 < 0 || len3 < 0){
      PERROR("<spdr%c>DirectRMW list is panic!", dev->id+'a');
      return -EINVAL;
    }

    c_size = dev->cache->n_sector*SPD_HARDSECT_SIZE;
    if(c_size > SPD_CACHE_BUFFER_SIZE){
      c_size = SPD_CACHE_BUFFER_SIZE;
    }

    if(len1 > 0){
      size = len1*SPD_HARDSECT_SIZE;
      offset = (cur_sector - dev->cache->sector)*SPD_HARDSECT_SIZE;

      b_offset = offset%c_size;
      b_size = (b_offset+size < c_size) ? size : (c_size - b_offset);
      i = offset/c_size;

      PINFO("[len1]off=%x b_off=%x b_size=%x i=%d", offset, b_offset, b_size, i);

      virt_addr = dev->cache->buffer[i] + b_offset;
      n = set_sg_entry(sg, n, virt_to_bus(virt_addr), b_size);
      if(unlikely(n < 0)){
	PERROR("<spdr%c>set_sg_entry() failed(%d)", dev->id+'a', n);
	return n;
      }
      size -= b_size;
      offset += b_size;
      i = offset/c_size;

      PINFO("[len1]off=%x size=%x i=%d", offset, size, i);

      while(size > 0){
	PINFO("[len1]n=%d size=%x", n, size);
	virt_addr = dev->cache->buffer[i++];
	b_size = (size<c_size?size:c_size);
	n = set_sg_entry(sg, n, virt_to_bus(virt_addr), b_size);
	if(unlikely(n < 0)){
	  PERROR("<spdr%c>set_sg_entry() failed(%d)", dev->id+'a', n);
	  return n;
	}
	size -= b_size;
      }
    }

    if(len2 > 0){
      size = len2*SPD_HARDSECT_SIZE;

      while(size > 0){
	u32 addr = le32_to_cpu((u32)entry[pos].sg_bus_address) + dev->bdev->rmw_sg_offset;
	b_size = (le32_to_cpu(entry[pos].sg_entry_size)) - dev->bdev->rmw_sg_offset;

	PINFO("[len2]b_size=%lx size=%lx", (unsigned long)b_size, (unsigned long)size);

	if(b_size > size){
	  PINFO("==");
	  n = set_sg_entry(sg, n, addr, size);
	  dev->bdev->rmw_sg_offset += size;

	  if(! endflag){
	    PERROR("<spdr%c>PANIC! endflag(%d) is OFF!", dev->id+'a', endflag);
	  }
	  break;
	}

	n = set_sg_entry(sg, n, addr, b_size);
	if(unlikely(n < 0)){
	  PERROR("<spdr%c>set_sg_entry() failed(%d)", dev->id+'a', n);
	  return n;
	}
	size -= b_size;
	dev->bdev->rmw_sg_offset = 0;
      }
    }
    
    if(endflag){
      PINFO("<spdr%c>endflag is ON", dev->id+'a');
      break;
    }

    cur_sector += (len1 + len2);
  }

  if (len3 > 0){
    size = len3*SPD_HARDSECT_SIZE;
    offset = ((cur_sector - dev->cache->sector) + len1+len2)*SPD_HARDSECT_SIZE;

    b_offset = offset%c_size;
    b_size   = c_size - b_offset;
    i = offset/c_size;

    PINFO("[len3]off=%x b_off=%x b_size=%x i=%d", offset, b_offset, b_size, i);

    virt_addr = dev->cache->buffer[i++] + b_offset;
    n = set_sg_entry(sg, n, virt_to_bus(virt_addr), b_size);
    if(unlikely(n < 0)){
      PERROR("<spdr%c>set_sg_entry() failed(%d)", dev->id+'a', n);
      return n;
    }
    size -= b_size;

    while(size > 0){
      virt_addr = dev->cache->buffer[i++];
      n = set_sg_entry(sg, n, virt_to_bus(virt_addr), c_size);
      if(unlikely(n < 0)){
	PERROR("<spdr%c>set_sg_entry() failed(%d)", dev->id+'a', n);
	return n;
      }
      size -= c_size;
    }
  }

  if(n == 0){
    PERROR("<spdr%c>making sg entries is failed!", dev->id+'a');
    return -EINVAL;
  }

  sg[n-1].count |= cpu_to_le32(SPD_SG_ENDMARK);
  dev->sg_n_entry = n;
  spd_dma_cache_wback(dev->dev, sg, sizeof(spd_scatterlist_t)*n);

  PRINT_SG(sg, n, dev->id);

  return 0;
}
