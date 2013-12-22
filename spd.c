/*
 P2Card driver common modules
 $Id: spd.c 250 2006-10-24 09:31:36Z hiraoka $
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <asm/uaccess.h>

#include "spd.h"
#include "spd_hwif.h"
#include "spd_bdev.h"
#include "spd_rdev.h"
#include "spd_udev.h"

#line __LINE__ "spd.c" /* Replace full path(__FILE__) to spd.c. */

#ifdef DBG_DMA_LED
int debug_dma_led = 0;
#endif /* DBG_DMA_LED */

#ifdef DBG_HOTPLUG_TEST
int debug_hotplug = 0;
module_param(debug_hotplug, int, S_IRUGO|S_IWUSR);
#endif /* DBG_HOTPLUG_TEST */

#if defined(DEBUG_PORT)
unsigned short *DEBUG_PORT_PTR = NULL;
#endif /* DEBUG_PORT */

spd_dev_t spd_dev[SPD_N_DEV];
spd_cache_t spd_cache[SPD_N_CACHE];

static void *sg[SPD_N_DEV];
static void *tmp_buf[SPD_N_DEV];
static void *cache_buf[SPD_N_CACHE*SPD_CACHE_N_BUFFER];
static void *cache_sg[SPD_N_CACHE];
spinlock_t cache_free_list_lock;
struct list_head cache_free_list[SPD_N_DEV];

static int cache_init(void);
static int cache_exit(void);

static int spd_module_init(void)
{
  int i;
  int retval;
  spd_dev_t *dev;
  PTRACE();

  printk(KERN_INFO "[spd]"SPD_VERSION"("SPD_P2PF_ARCH")");
#if defined(SPD_OPTION_VER)
  printk("-"SPD_OPTION_VER);
#endif /* SPD_OPTION_VER */
  printk("\n");
  
#if defined(DEBUG_PORT)
  DEBUG_PORT_PTR = ioremap(DEBUG_PORT, 2);
  if(unlikely(NULL == DEBUG_PORT_PTR)){
    printk(KERN_ERR DBG_SYS_NAME "ioremap failed at %s\n", __FUNCTION__);
  }
#endif /* DEBUG_PORT */
  HOTPLUG_IN(0);

  retval = 0;
  memset(spd_dev,   0, sizeof(spd_dev));
  memset(sg,        0, sizeof(sg));
  memset(tmp_buf,   0, sizeof(tmp_buf));

  for(i = 0; i < SPD_N_DEV; i++){
    sg[i] = kmalloc(SPD_SG_SIZE, GFP_KERNEL);
    if(unlikely(sg[i] == NULL)){
      retval = -ENOMEM;
      goto FAIL;
    }

    tmp_buf[i] = kmalloc(SPD_HARDSECT_SIZE, GFP_KERNEL);
    if(unlikely(tmp_buf[i] == NULL)){
      retval = -ENOMEM;
      goto FAIL;
    }
  }
  retval = cache_init();
  if(unlikely(retval < 0)) goto FAIL;
  
  for(i = 0; i < SPD_N_DEV; i++){
    dev = &spd_dev[i];
    spin_lock_init(&dev->lock);
    sema_init(&dev->io_sem, 1);
    atomic_set(&dev->status, 0);
    dev->id               = i;
    dev->n_area           = 0;
    dev->dma_timeout      = 0;
    dev->dma_retry        = 0;
    dev->errcode          = 0;
    dev->time_stamp       = 0;
    dev->ticks            = 0;
    dev->timeout          = SPD_CMD_TIMEOUT;
    dev->retry            = SPD_NO_RETRY;
    dev->sg               = sg[i];
    dev->sg_n_entry       = 0;

    dev->tmp_buf          = tmp_buf[i];
    dev->complete_handler = NULL;
  }
  
  retval = spd_bdev_init();
  if(unlikely(retval < 0)){
    PERROR("spd_bdev_init() failed(%d)", retval);
    goto FAIL;
  }

  retval = spd_rdev_init();
  if(unlikely(retval < 0)){
    PERROR("spd_rdev_init() failed(%d)", retval);
    spd_bdev_exit();
    goto FAIL;
  }

  retval = spd_udev_init();
  if(unlikely(retval < 0)){
    PERROR("spd_udev_init() failed(%d)", retval);
    spd_rdev_exit();
    spd_bdev_exit();
    goto FAIL;
  }

  retval = spd_hwif_init();
  if(unlikely(retval < 0)){
    PERROR("spd_hwif_init() failed(%d)", retval);
    spd_udev_exit();
    spd_rdev_exit();
    spd_bdev_exit();
    goto FAIL;
  }

  return 0;

 FAIL:
  cache_exit();
  for(i=0; i < SPD_N_DEV; i++){
    if(sg[i] != NULL){
      kfree(sg[i]);
    }
    if(tmp_buf[i] != NULL){
      kfree(tmp_buf[i]);
    }
  }
  return retval;
}


static void __exit spd_module_exit(void)
{
  int i;
  PTRACE();

  spd_hwif_exit();
  spd_udev_exit();
  spd_rdev_exit();
  spd_bdev_exit();
  cache_exit();

  for(i=0; i < SPD_N_DEV; i++){
    if(sg[i] != NULL){
      kfree(sg[i]);
    }
    if(tmp_buf[i] != NULL){
      kfree(tmp_buf[i]);
    }
  }

#if defined(DEBUG_PORT)
  if(DEBUG_PORT_PTR){
    iounmap(DEBUG_PORT_PTR);
    DEBUG_PORT_PTR = NULL;
  }
#endif /* DEBUG_PORT */
}


static inline void cache_sg_init(spd_cache_t *cache)
{
  spd_scatterlist_t *sg = cache->sg;
  int i,j;
  int n;
  u8 *virt_addr;
  PTRACE();

  n = 0;
  for(i = 0; i < SPD_CACHE_N_BUFFER; i++){
    virt_addr = cache->buffer[i];
    for(j = 0; j < SPD_CACHE_BUFFER_SIZE/SPD_SG_MAX_COUNT; j++){
      sg[n].bus_address = cpu_to_le32(virt_to_bus(virt_addr));
      sg[n].count       = cpu_to_le32(SPD_SG_MAX_COUNT);
      virt_addr        += SPD_SG_MAX_COUNT;
      n++;
    }
  }
}


static int cache_init(void)
{
  spd_cache_t *cache;
  int i, j;
  int n = 0, retval = 0;
  PTRACE();

  memset(spd_cache, 0, sizeof(spd_cache));
  memset(cache_buf, 0, sizeof(cache_buf));
  memset(cache_sg,  0, sizeof(cache_sg));
  spin_lock_init(&cache_free_list_lock);

  for(i = 0; i < SPD_N_DEV; i++){
    INIT_LIST_HEAD(&cache_free_list[i]);
  }

  for(i = 0; i < SPD_N_CACHE*SPD_CACHE_N_BUFFER; i++){
    cache_buf[i] = (void *)__get_free_pages(GFP_KERNEL, SPD_CACHE_BUFFER_ORDER);
    if(unlikely(cache_buf[i] == NULL)){
      PERROR("__get_free_pages() failed(i=%d)", i);
      retval = -ENOMEM;
      goto FAIL;
    }
  }

  for(i = 0; i < SPD_N_CACHE; i++){
    cache_sg[i] = (void *)kmalloc(SPD_CACHE_SG_SIZE, GFP_KERNEL);
    if(unlikely(cache_sg[i] == NULL)){
      retval = -ENOMEM;
      goto FAIL;
    }
  }

  for(i = 0; i < SPD_N_CACHE; i++){
    cache = &spd_cache[i];
    for(j = 0; j < SPD_CACHE_N_BUFFER; j++){
      cache->buffer[j] = cache_buf[n++];
    }
    cache->id     = i;
    cache->owner  = NULL;
    cache->sector = -1;
    cache->sg     = cache_sg[i];
    cache_sg_init(cache);
    INIT_LIST_HEAD(&cache->list);
    list_add(&cache->list, &cache_free_list[i%SPD_N_DEV]);
  }

  return 0;

 FAIL:
  cache_exit();
  return retval;
}

  
static int cache_exit(void)
{
  int i;
  PTRACE();

  for(i = 0; i < SPD_N_CACHE*SPD_CACHE_N_BUFFER; i++){
    if(cache_buf[i] != NULL){
      free_pages((u32)cache_buf[i], SPD_CACHE_BUFFER_ORDER);
    }
  }

  for(i = 0; i < SPD_N_CACHE; i++){
    if(cache_sg[i] != NULL){
      kfree(cache_sg[i]);
    }
  }

  return 0;
}


spd_cache_t *spd_cache_alloc(spd_dev_t *dev)
{
  unsigned long flags = 0;
  int i;
  spd_cache_t *cache = NULL;
  PTRACE();

  spin_lock_irqsave(&cache_free_list_lock, flags);

  for(i = dev->id; i < SPD_N_DEV; i++){
    if(!list_empty(&cache_free_list[i])) goto FOUND;
  }
  for(i = 0; i < dev->id; i++){
    if(!list_empty(&cache_free_list[i])) goto FOUND;
  }
  spin_unlock_irqrestore(&cache_free_list_lock, flags);
  return NULL;

 FOUND:
  cache = list_entry(cache_free_list[i].next, spd_cache_t, list);
  list_del_init(&cache->list);
  if(cache->owner != dev){
    cache->owner = dev;
    cache->sector = -1;
  }
  spin_unlock_irqrestore(&cache_free_list_lock, flags);

  return cache;
}


void spd_cache_release(spd_dev_t *dev, spd_cache_t *cache)
{
  unsigned long flags = 0;
  PTRACE();
  spin_lock_irqsave(&cache_free_list_lock, flags);
  list_add(&cache->list, &cache_free_list[dev->id]);
  spin_unlock_irqrestore(&cache_free_list_lock, flags);
}


void spd_cache_invalidate(spd_dev_t *dev)
{
  unsigned long flags = 0;
  struct list_head *ptr;
  spd_cache_t *cache;
  PTRACE();

  spin_lock_irqsave(&cache_free_list_lock, flags);

  if(dev->cache){
    if(spd_cache_is_dirty(dev->cache)){
      PERROR("<spd%c>invalidate dirty cache sector=%08x, count=%04x",
             dev->id+'a', dev->cache->sector, dev->cache->n_sector);
      spd_cache_clr_dirty(dev->cache);
    }
    dev->cache->sector = -1;
  }
  
  list_for_each(ptr, &cache_free_list[dev->id]){
    cache = list_entry(ptr, spd_cache_t, list);
    cache->sector = -1;
  }
  spin_unlock_irqrestore(&cache_free_list_lock, flags);
}


void spd_send_event(spd_dev_t *dev, u32 event, unsigned long arg)
{
  PINFO("<spd%c>event=%d, arg=%ld",dev->id+'a', ffs(event)-1, arg);

#if defined(SPD_CONFIG_RAW)
  if(dev->rdev->event_mask&event){
    dev->rdev->event_handler(dev, event, arg);
  }
#endif /* SPD_CONFIG_RAW */

  if(dev->bdev->event_mask&event){
    dev->bdev->event_handler(dev, event, arg);
  }
}


module_init(spd_module_init);
module_exit(spd_module_exit);
MODULE_LICENSE("GPL");
