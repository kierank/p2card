/*
 P2card control driver private header
 $Id: spd_rdev.h 187 2006-06-13 04:48:32Z hiraoka $
 */
#ifndef _SPD_RDEV_H
#define _SPD_RDEV_H

#ifdef __KERNEL__
#include <linux/kernel.h>
#include <linux/wait.h>
#include <linux/completion.h>
#include <linux/blkdev.h>
#include <linux/cdev.h>

enum RDEV_DEFAULT_ENUM {
  SPD_RDEV_MAJOR = 240,
};


typedef struct _spd_rdev_private_t {
  int errcode;
  u32 events;
  u32 event_mask;
  void (*event_handler)(spd_dev_t *dev, u32 event, unsigned long arg);
  struct completion dma;
  wait_queue_head_t pollwq;
  struct cdev cdev;
} spd_rdev_t;


#if defined(SPD_CONFIG_RAW)
/* spd_rdev.c */
int spd_rdev_init(void);
int spd_rdev_exit(void);
#else /* !SPD_CONFIG_RAW */
static inline int spd_rdev_init(void){return 0;}
static inline int spd_rdev_exit(void){return 0;}
#endif /* SPD_CONFIG_RAW */

#endif /* __KERNEL__ */
#endif /* _SPD_RDEV_H */

