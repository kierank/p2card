/*
 P2card block device private header
 $Id: spd_bdev.h 223 2006-09-19 11:03:44Z hiraoka $
 */
#ifndef _SPD_BDEV_H
#define _SPD_BDEV_H
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

#line __LINE__ "spd_bdev.h" /* Replace full path(__FILE__) to spd_bdev.h. */

enum BDEV_DEFAULT_ENUM {
  SPD_BDEV_MAJOR = 240,
};


typedef struct _spd_bdev_private_t {
  struct list_head list;
  u32 event_mask;
  void (*event_handler)(spd_dev_t *dev, u32 event, unsigned long arg);
  int usage;

  int media_changed;
  struct gendisk *gd;
  spinlock_t rq_lock;
  struct request *req;
  struct request_queue *queue;
  u32 hard_nr_sectors;

  struct scatterlist sg[SPD_SG_N_ENTRY];

  u32 rmw_sector;
  u32 rmw_count;
  struct scatterlist *rmw_sg;
  int rmw_sg_offset;
  int n_map;

  struct bio *rmw_bio;
  int rmw_bio_offset;
  int rmw_bio_idx;
  int rmw_bvec_offset;

  struct tasklet_struct tasklet;
} spd_bdev_t;


/* spd_bdev.c */
int spd_bdev_init(void);
int spd_bdev_exit(void);
void bdev_end_request(spd_dev_t *dev, int errcode);

static inline u32 align_sector(u32 sector, u32 wsize, u32 offset)
{
  u32 mask = ~(wsize-1);
  return (u32)(((sector-offset)&mask) + offset);
}

static inline int set_sg_entry(spd_scatterlist_t *sg, int n,
                               u32 bus_addr, u32 count)
{
  u32 part;

  if(unlikely(n >= SPD_SG_N_ENTRY)){
    PERROR("scatterlist buffer overflow(%d)", n);
    return -ENOMEM;
  }

  if(n > 0 && 
     le32_to_cpu(sg[n-1].bus_address)+le32_to_cpu(sg[n-1].count) == bus_addr &&
     le32_to_cpu(sg[n-1].count)+count <= SPD_SG_MAX_COUNT){
    
    sg[n-1].count = cpu_to_le32(le32_to_cpu(sg[n-1].count) + count);
    return n;
  }

  while(count){
    part = count>SPD_SG_MAX_COUNT?SPD_SG_MAX_COUNT:count;
    sg[n].bus_address  = cpu_to_le32(bus_addr);
    sg[n].count = cpu_to_le32(part);
    bus_addr += part;
    count -= part;
    n++;
    if(unlikely(n >= SPD_SG_N_ENTRY)){
      PERROR("scatterlist buffer overflow(%d)", n);
      return -ENOMEM;
    }
  }

  return n;
}


#if ! (KERNEL_VERSION(2,6,25) <= LINUX_VERSION_CODE) /* KERNEL_VERSION: - 2.6.24 */
# define sg_page(sg) (sg->page)
#endif /* KERNEL_VERSION : - 2.6.24 */

/* spd_drct.c */
int drct_read(spd_dev_t *dev);
int drct_write(spd_dev_t *dev);
int drct_read_modify_write(spd_dev_t *dev);

#endif /* __KERNEL__  */
#endif /* _SPD_BDEV_H */
