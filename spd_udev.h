/*
 P2Card USB driver private header
 $Id:$
 */
#ifndef _SPD_UDEV_H
#define _SPD_UDEV_H

#ifdef __KERNEL__
#include <linux/kernel.h>
#include <linux/wait.h>
#include <linux/completion.h>
#include <linux/cdev.h>


enum UDEV_DEFAULT_ENUM {
  SPD_UDEV_MAJOR  = 241,
  SPD_UDEV_READY  = 0,
  SPD_UDEV_READ   = 1,
  SPD_UDEV_WRITE  = 2,
  SPD_UDEV_DONE   = 3,
  SPD_UDEV_ERROR  = -1,
};


typedef struct _spd_udev_private_t {
  u32 event_mask;
  void (*event_handler)(spd_dev_t *dev, u32 event, unsigned long arg);
  int status;
  int errcode;
  u32 transfer_length;
  wait_queue_head_t pollwq;
  struct cdev cdev;
} spd_udev_t;


#if defined(SPD_CONFIG_USB)
/* spd_udev.c */
int spd_udev_init(void);
int spd_udev_exit(void);
#else /* !SPD_CONFIG_USB */
static inline int spd_udev_init(void){return 0;}
static inline int spd_udev_exit(void){return 0;}
#endif /* SPD_CONFIG_USB */

#endif /* __KERNEL__ */
#endif /* _SPD_UDEV_H */
