/*
 P2 card driver direct transfer modules
 $Id:$
*/

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

#include "spd.h"
#include "spd_bdev.h"

#line __LINE__ "spd_drct.c" /* Replace full path(__FILE__) to spd_drct.c. */

static int  drct_make_sg(spd_dev_t *dev);
static int  drct_make_rmw_sg(spd_dev_t *dev);
static int  drct_set_sg_entry(spd_dev_t *dev, int n, struct bio *bio, u32 count);
int  drct_read(spd_dev_t *dev);
static void drct_read_complete_handler(spd_dev_t *dev);
int  drct_write(spd_dev_t *dev);
static void drct_write_complete_handler(spd_dev_t *dev);
int  drct_read_modify_write(spd_dev_t *dev);
static void drct_rmw_read_complete_handler(spd_dev_t *dev);
static void drct_rmw_write_complete_handler(spd_dev_t *dev);


int drct_read(spd_dev_t *dev)
{
  int retval;
  u32 sector;
  u16 count;
  unsigned long flags = 0;
  struct request *req = dev->bdev->req;
  PTRACE();

  dev->bdev->hard_nr_sectors = req->hard_nr_sectors;

  retval = drct_make_sg(dev);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>drct_make_sg() failed(%d)", dev->id+'a', retval);
    goto ABORT;
  }

  sector                = req->sector;
  count                 = req->hard_nr_sectors;
  dev->retry            = dev->dma_retry;
  dev->timeout          = dev->dma_timeout;
  dev->complete_handler = drct_read_complete_handler;
  retval = spd_read_sector(dev, sector, count, dev->sg);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>spd_read_sector() failed(%d)", dev->id+'a', retval);
    goto ABORT;
  }

  return 0;

ABORT:
  PINFO("<spd%c>ABORT at %s", dev->id+'a', __FUNCTION__);
  spin_lock_irqsave(&dev->bdev->rq_lock, flags);
  bdev_end_request(dev, retval);
  spin_unlock_irqrestore(&dev->bdev->rq_lock, flags);

  dev->complete_handler = NULL;
  spd_io_unlock(dev);

  return retval;
}


static void drct_read_complete_handler(spd_dev_t *dev)
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


int drct_write(spd_dev_t *dev)
{
  int retval;
  u32 sector;
  u16 count;
  unsigned long flags = 0;
  struct request *req = dev->bdev->req;
  PTRACE();

  dev->bdev->hard_nr_sectors = req->hard_nr_sectors;

  retval = drct_make_sg(dev);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>drct_make_sg() failed(%d)", dev->id+'a', retval);
    goto ABORT;
  }

  sector = req->sector;
  count  = req->hard_nr_sectors;

  if(sector == dev->cache->sector){
    dev->cache->sector = (u32)-1;
    spd_cache_clr_dirty(dev->cache);
  }

  dev->retry            = dev->dma_retry;
  dev->timeout          = dev->dma_timeout;
  dev->complete_handler = drct_write_complete_handler;

  if(rq_is_seq(req)){
    retval = spd_seq_write_sector(dev, sector, count, dev->sg);
    if(unlikely(retval < 0)){
      PERROR("<spd%c>spd_seq_write_sector() failed(%d)", dev->id+'a', retval);
      goto ABORT;
    }

    return 0;
  }

  retval = spd_write_sector(dev, sector, count, dev->sg);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>spd_write_sector() failed(%d)", dev->id+'a', retval);
    goto ABORT;  
  }
  return 0;

ABORT:
  PINFO("<spd%c>ABORT at %s", dev->id+'a', __FUNCTION__);
  spin_lock_irqsave(&dev->bdev->rq_lock, flags);
  bdev_end_request(dev, retval);
  spin_unlock_irqrestore(&dev->bdev->rq_lock, flags);

  dev->complete_handler = NULL;
  spd_io_unlock(dev);

  return retval;
}


static void drct_write_complete_handler(spd_dev_t *dev)
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


int drct_read_modify_write(spd_dev_t *dev)
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
  dev->bdev->rmw_bio         = req->bio;
  dev->bdev->rmw_bio_offset  = 0;
  dev->bdev->rmw_bvec_offset = 0;
  dev->bdev->rmw_bio_idx     = req->bio->bi_idx;

  dev->retry   = dev->dma_retry;
  dev->timeout = dev->dma_timeout;

  if(spd_cache_is_dirty(dev->cache)){
    dev->complete_handler = drct_rmw_write_complete_handler;
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
    retval = drct_make_rmw_sg(dev);

    if(unlikely(retval < 0)){
      PERROR("<spd%c>drct_make_rmw_sg() failed(%d)", dev->id+'a', retval);
      goto ABORT;
    }

    dev->complete_handler = drct_rmw_write_complete_handler;
    retval = spd_write_sector(dev, sector, wsize, dev->sg);
    if(unlikely(retval < 0)){
      PERROR("<spd%c>spd_write_sector() failed(%d)", dev->id+'a', retval);
      goto ABORT;
    }

    dev->cache->sector = (u32)-1;
    spd_cache_clr_dirty(dev->cache);

    return 0;
  }

  dev->cache->sector   = sector;
  dev->cache->n_sector = wsize;

  dev->complete_handler = drct_rmw_read_complete_handler;
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


static void drct_rmw_read_complete_handler(spd_dev_t *dev)
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

  retval = drct_make_rmw_sg(dev);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>drct_make_rmw_sg() failed(%d)", dev->id+'a', retval);
    goto ABORT;
  }

  dev->complete_handler = drct_rmw_write_complete_handler;
  retval = spd_write_sector(dev, 
                            dev->cache->sector,
                            dev->cache->n_sector,
                            dev->sg);
  if(unlikely(retval < 0)){
    PERROR("<spd%c>spd_write_sector() failed(%d)", dev->id+'a', retval);
    goto ABORT;
  }

  dev->cache->sector = (u32)-1;
  spd_cache_clr_dirty(dev->cache);

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


static void drct_rmw_write_complete_handler(spd_dev_t *dev)
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

  if(dev->bdev->rmw_count == 0){
    spin_lock_irqsave(&dev->bdev->rq_lock, flags);
    bdev_end_request(dev, 0);
    spin_unlock_irqrestore(&dev->bdev->rq_lock, flags);

    dev->complete_handler = NULL;
    spd_io_unlock(dev);

    return;
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
    retval = drct_make_rmw_sg(dev);
    if(unlikely(retval < 0)){
      PERROR("<spd%c>drct_make_rmw_sg() failed(%d)", dev->id+'a', retval);
      goto ABORT;
    }

    dev->complete_handler = drct_rmw_write_complete_handler;
    retval = spd_write_sector(dev, sector, wsize, dev->sg);
    if(unlikely(retval < 0)){
      PERROR("<spd%c>spd_write_sector() failed(%d)", dev->id+'a', retval);
      goto ABORT;
    }

    dev->cache->sector = (u32)-1;
    spd_cache_clr_dirty(dev->cache);

    return;
  }

  dev->cache->sector   = sector;
  dev->cache->n_sector = wsize;

  dev->complete_handler = drct_rmw_read_complete_handler;
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


static int drct_make_sg(spd_dev_t *dev)
{
  spd_scatterlist_t *sg = dev->sg;
  struct request *req = dev->bdev->req;
  struct bio *bio = NULL;
  int n = 0;
  PTRACE();

  for(bio = req->bio; bio; bio = bio->bi_next){
    dev->bdev->rmw_bio_idx = bio->bi_idx;
    n = drct_set_sg_entry(dev, n, bio, bio->bi_size);
    if(unlikely(n < 0)){
      PERROR("<spd%c>drct_set_sg_entry() failed(%d)",dev->id+'a', n);
      return n;
    }
  }
  sg[n-1].count |= cpu_to_le32(SPD_SG_ENDMARK);
  dev->sg_n_entry = n;
  spd_dma_cache_wback(dev->dev, sg, sizeof(spd_scatterlist_t)*n);

  PRINT_SG(sg, n, dev->id);

  return 0;
}


static int drct_make_rmw_sg(spd_dev_t *dev)
{
  spd_scatterlist_t *sg = dev->sg;
  struct bio *bio = dev->bdev->rmw_bio;
  int n,i;
  int len1, len2, len3;
  u8  *virt_addr;
  u32 b_size, size, offset, b_offset;
  u32 c_size;
  PTRACE();

  PINFO("<spd%c>cache_sector=%08x, rmw_sector=%08x, rmw_count=%04x",
        dev->id+'a',
	dev->cache->sector, 
        dev->bdev->rmw_sector,
        dev->bdev->rmw_count);

  PCOMMAND("<spd%c>USE_CACHE id=%02d, cache_sector=%08x, rmw_sector=%08x, rmw_count=%04x",
           dev->id+'a',
           dev->cache->id,
           dev->cache->sector,
           dev->bdev->rmw_sector,
           dev->bdev->rmw_count);

  n = 0;
  len1 = (dev->bdev->rmw_sector - dev->cache->sector);
  if(len1 + dev->bdev->rmw_count > dev->cache->n_sector){
    len2 = dev->cache->n_sector - len1;
    len3 = 0;

    dev->bdev->rmw_count -= len2;
    dev->bdev->rmw_sector = dev->cache->sector+dev->cache->n_sector;
  } else {
    len2 = dev->bdev->rmw_count;
    len3 = dev->cache->n_sector -len2 - len1;

    dev->bdev->rmw_count  = 0;
    dev->bdev->rmw_sector = (u32)-1;
  };

  PINFO("<spd%c>len1=%d, len2=%d, len3=%d", dev->id+'a', len1, len2, len3);

  c_size = dev->cache->n_sector*SPD_HARDSECT_SIZE;
  if(c_size > SPD_CACHE_BUFFER_SIZE){
    c_size = SPD_CACHE_BUFFER_SIZE;
  }

  if (len1 > 0){
    i = 0;
    size = len1*SPD_HARDSECT_SIZE;
    while(size > 0){
      virt_addr = dev->cache->buffer[i++];
      b_size    = (size<c_size?size:c_size);
      n = set_sg_entry(sg, n, virt_to_bus(virt_addr), b_size);
      if(unlikely(n < 0)){
        PERROR("<spd%c>set_sg_entry() failed(%d)", dev->id+'a', n);
        return n;
      }
      size -= b_size;
    }
  }

  if(len2 > 0){
    size = len2*SPD_HARDSECT_SIZE;
    bio  = dev->bdev->rmw_bio;

    while(size > 0){
      b_size = bio->bi_size - dev->bdev->rmw_bio_offset;

      PINFO("b_size=%lx size=%lx maxvec=%d",
	    (unsigned long)b_size, (unsigned long)size, bio->bi_max_vecs);

      if(b_size > size){
	PINFO("==");
	n = drct_set_sg_entry(dev, n, bio, size);
	dev->bdev->rmw_bio_offset += size;
        break;
      }
      n = drct_set_sg_entry(dev, n, bio, b_size);
      if(unlikely(n < 0)){
        PERROR("<spd%c>drct_set_sg_entry() failed(%d)",dev->id+'a', n);
        return n;
      }
      size -= b_size;
      bio   = bio->bi_next;
      dev->bdev->rmw_bio         = bio;
      dev->bdev->rmw_bio_offset  = 0;
      dev->bdev->rmw_bvec_offset = 0;
      dev->bdev->rmw_bio_idx     = bio ? bio->bi_idx : 0;
    }
  }

  if (len3 > 0){
    size   = len3*SPD_HARDSECT_SIZE;
    offset = (len1+len2)*SPD_HARDSECT_SIZE;

    b_offset = offset%c_size;
    b_size   = c_size - b_offset;
    i        = offset/c_size;

    virt_addr = dev->cache->buffer[i++] + b_offset;
    n = set_sg_entry(sg, n, virt_to_bus(virt_addr), b_size);
    if(unlikely(n < 0)){
      PERROR("<spd%c>set_sg_entry() failed(%d)",dev->id+'a', n);
      return n;
    }
    size -= b_size;

    while(size > 0){
      virt_addr = dev->cache->buffer[i++];
      n = set_sg_entry(sg, n, virt_to_bus(virt_addr), c_size);
      if(unlikely(n < 0)){
        PERROR("<spd%c>set_sg_entry() failed(%d)",dev->id+'a', n);
        return n;
      }
      size -= c_size;
    }
  }

  sg[n-1].count |= cpu_to_le32(SPD_SG_ENDMARK);
  dev->sg_n_entry = n;
  spd_dma_cache_wback(dev->dev, sg, sizeof(spd_scatterlist_t)*n);

  PRINT_SG(sg, n, dev->id);

  return 0;
}


static int drct_set_sg_entry(spd_dev_t *dev, int n, struct bio *bio, u32 count)
{
  spd_scatterlist_t *sg = dev->sg;
  spd_bdev_t *bdev = dev->bdev;
  struct bio_vec *bvec = bio_iovec_idx(bio, bdev->rmw_bio_idx);
  u32 bus_addr = 0;
  u32 size = 0;
  int i = bdev->rmw_bio_idx;

  PINFO("idx=%d n=%d count=%lx", bdev->rmw_bio_idx, n, (unsigned long)count);

  while(count > 0){
    bvec = bio_iovec_idx(bio, i);

#if defined(CONFIG_P2FAT_FS)
    if(bio_drct(bio)){
      /* Cannot touch bvec->bv_page(=NULL)!!
       * bvec->bv_private(append new) = bus address!?
       * bvec->bv_len = data size
       *  maybe offset == 0 */
      bus_addr = bvec->bv_private + bdev->rmw_bvec_offset;
      size     = bvec->bv_len - bdev->rmw_bvec_offset;

      PRINT_BVEC(i, bus_addr, size, bdev->rmw_bvec_offset, n, 1);

      if(size > count){
	PINFO("size=%lx count=%lx", (unsigned long)size, (unsigned long)count);
	n = set_sg_entry(sg, n, bus_addr, count);
	bdev->rmw_bvec_offset += count;
	bdev->rmw_bio_idx      = i;
	break;
      }
      n = set_sg_entry(sg, n, bus_addr, size);
#else /* ! CONFIG_P2FAT_FS */
    if (0) {
      ;
#endif /* CONFIG_P2FAT_FS */

    } else {
      /* Use bvec->bv_page (nomal style) */
      unsigned long flags = 0;
      char *buffer = bvec_kmap_irq(bvec, &flags);

      bus_addr = virt_to_bus((void *)buffer) + bdev->rmw_bvec_offset;
      size     = bvec->bv_len - bdev->rmw_bvec_offset;

      PRINT_BVEC(i, bus_addr, size, bdev->rmw_bvec_offset, n, 0);

      if(size > count){
	PINFO("size=%lx count=%lx", (unsigned long)size, (unsigned long)count);
	n = set_sg_entry(sg, n, bus_addr, count);
	bdev->rmw_bvec_offset += count;
	bdev->rmw_bio_idx      = i;
	bvec_kunmap_irq(buffer, &flags);
	break;
      }
      n = set_sg_entry(sg, n, bus_addr, size);
      bvec_kunmap_irq(buffer, &flags);
    }

    if(n < 0){
      return n;
    }
    i++;
    count -= size;

    bdev->rmw_bvec_offset = 0;
    bdev->rmw_bio_idx     = i;
  }

  return n;
}
