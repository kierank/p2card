/*
 Debug macros
 $Id: debug.h 223 2006-09-19 11:03:44Z hiraoka $
 */
#ifndef _DEBUG_H
#define _DEBUG_H

#line __LINE__ "debug.h" /* Replace full path(__FILE__) to debug.h. */

#ifndef  DBG_SYS_NAME
# define DBG_SYS_NAME  "[kernel]"
#endif /* !DBG_SYS_NAME */

#define DL_PANIC     1
#define DL_ALERT     2
#define DL_CRITICAL  3
#define DL_ERROR     4
#define DL_WARNING   5
#define DL_NOTICE    6
#define DL_INFO      7
#define DL_DEBUG     8

#ifndef DBG_LEVEL
# define DBG_LEVEL DL_INFO
#endif /* !DBG_LEVEL */

#define PPANIC(fmt, args...)				\
  printk(KERN_EMERG DBG_SYS_NAME "1:%s:%d:" fmt "\n",	\
	 __FILE__, __LINE__, ## args);

#define PALERT(fmt, args...)				\
  printk(KERN_ALERT DBG_SYS_NAME "2:%s:%d:" fmt "\n",	\
	 __FILE__, __LINE__, ## args);

#define PCRITICAL(fmt, args...)				\
  printk(KERN_CRIT DBG_SYS_NAME "3:%s:%d:" fmt "\n",	\
	 __FILE__, __LINE__, ## args);

#define PERROR(fmt, args...)				\
  printk(KERN_ERR DBG_SYS_NAME "4:%s:%d:" fmt "\n",	\
	 __FILE__, __LINE__, ## args);

#define PWARNING(fmt, args...)				\
  printk(KERN_WARNING DBG_SYS_NAME "5:%s:%d:" fmt "\n",	\
	 __FILE__, __LINE__, ## args);

#define PNOTICE(fmt, args...)				\
  printk(KERN_NOTICE DBG_SYS_NAME "6:%s:%d:" fmt "\n",	\
	 __FILE__, __LINE__, ## args);

#define PINFO(fmt, args...)				\
  printk(KERN_INFO DBG_SYS_NAME "7:%s:%d:" fmt "\n",	\
	 __FILE__, __LINE__, ## args);

#define PDEBUG(fmt, args...)				\
  printk(KERN_DEBUG DBG_SYS_NAME "8:%s:%d:" fmt "\n",	\
	 __FILE__, __LINE__, ## args);

#if DBG_LEVEL < DL_PANIC
#undef PPANIC
# define PPANIC(fmt, args...)
#endif /* DL_PANIC */

#if DBG_LEVEL < DL_ALERT
#undef PALERT
# define PALERT(fmt, args...)
#endif /* DL_ALERT */

#if DBG_LEVEL < DL_CRITICAL
#undef PCRITICAL
# define PCRITICAL(fmt, args...)
#endif /* DL_CRITICAL */

#if DBG_LEVEL < DL_ERROR
#undef PERROR
# define PERROR(fmt, args...)
#endif /* DL_ERROR */

#if DBG_LEVEL < DL_WARNING
#undef PWARNING
# define PWARNING(fmt, args...)
#endif /* DL_WARNING */

#if DBG_LEVEL < DL_NOTICE
#undef PNOTICE
# define PNOTICE(fmt, args...)
#endif /* DL_NOTICE */

#if DBG_LEVEL < DL_INFO
#undef PINFO
# define PINFO(fmt, args...)
#endif /* DL_INFO */

#if DBG_LEVEL < DL_DEBUG
#undef PDEBUG
# define PDEBUG(fmt, args...)
#endif /* DL_DEBUG */


#if defined(DBG_TRACE)
# define PTRACE(fmt, args...) printk(KERN_INFO DBG_SYS_NAME ">>>>%s" fmt "\n",\
				     __FUNCTION__, ## args);
#else /* !DBG_TRACE */
# define PTRACE(fmt, args...)
#endif /* DBG_TRACE */

#if defined(DBG_ASSERT)
# define PASSERT(cond)\
{\
  if(cond) PALERT("Assert");\
}
#else /* !DBG_ASSERT */
# define PASSERT(cond)
#endif /* DBG_ASSERT */

/* #if DBG_LEVEL > DL_WARNING */
#if defined(DBG_DELAYPROC)
#include "spd_bdev.h"


static inline void PRINT_COPY_CACHE(spd_dev_t *dev)
{
  struct request *req = dev->bdev->req;
  spd_cache_t *cache = dev->cache;
  int type;

  if(req == NULL|| cache == NULL){
    return;
  }

  if(rq_is_drct(req)){
    type = 'D';
    BUG();
  } else if(rq_is_rt(req) || rq_is_fat(req)){
    type = 'A';
  } else {
    type = 'N';
  }

  PNOTICE("<spd%c>(%c)COPY CACHE sector=%08x rmw_sector=%08x count=%04x",
          dev->id+'a', 
          type,
          cache->sector,
          dev->bdev->rmw_sector,
          dev->bdev->rmw_count);
}


static inline void PRINT_WRITE_CACHE(spd_dev_t *dev)
{
  spd_cache_t *cache = dev->cache;
  if(cache == NULL){
    return;
  }
  PNOTICE("<spd%c>(N)WRITE DMA sector=%08x count=%04x",
          dev->id+'a', 
          cache->sector,
          cache->n_sector);
}

static inline void PRINT_REQUEST(spd_dev_t *dev, int cmd, u32 sector, u16 count)
{
  struct request *req = dev->bdev->req;
  int type;
  if(req == NULL){
    return;
  }

  if(rq_is_drct(req)){
    type = 'D';
  } else if(rq_is_rt(req) || rq_is_fat(req)){
    type = 'A';
  } else {
    type = 'N';
  }

  if(cmd == SPD_DIR_READ){
    PNOTICE("<spd%c>(%c)READ DMA sector=%08x count=%04x",
            dev->id+'a', type, sector, count);
  } else if(rq_is_drct(req)&&rq_is_seq(req)){
    PNOTICE("<spd%c>(%c)SEQ WRITE DMA sector=%08x count=%04x",
            dev->id+'a', type, sector, count);
  } else {
    PNOTICE("<spd%c>(%c)WRITE DMA sector=%08x count=%04x",
            dev->id+'a', type, sector, count);
  }
}
#else /* !DBG_DELAYPROC */
# define PRINT_REQUEST(dev, cmd, sector, count)
# define PRINT_COPY_CACHE(dev)
# define PRINT_WRITE_CACHE(dev)
#endif /* DBG_DELAYPROC */


#if defined(DEBUG_PORT) && defined(DBG_DMA_LED)
extern int debug_dma_led;
static inline void  DMA_LED_ON(void)
{
  spd_debug_port(++debug_dma_led);
}


static inline void DMA_LED_OFF(void)
{ 
  spd_debug_port(--debug_dma_led);
}
#else /* !(DEBUG_PORT && DBG_DMA_LED) */
# define DMA_LED_ON()
# define DMA_LED_OFF()
#endif /* DEBUG_PORT && DBG_DMA_LED */


#if defined(DEBUG_PORT) && defined(DBG_HOTPLUG_TEST)
extern int debug_hotplug;
inline static void  HOTPLUG_IN(int n)
{
  if(n == debug_hotplug || n == 0){
    spd_debug_port(1);
    printk(KERN_WARNING DBG_SYS_NAME "hotplug in:%d\n", n);
  }
}


inline static void  HOTPLUG_OUT(int n)
{
  if(n == debug_hotplug || n == 0){
    spd_debug_port(0);
    printk(KERN_WARNING DBG_SYS_NAME "hotplug out:%d\n", n);
  }
}
#else /* !(DEBUG_PORT && DBG_HOTPLUG_TEST) */
# define HOTPLUG_IN(n)
# define HOTPLUG_OUT(n)
#endif /* DEBUG_PORT && DBG_HOTPLUG_TEST */


#if defined(DBG_COMMAND_PRINT)
# define PCOMMAND(fmt, args...)				\
  printk(KERN_WARNING DBG_SYS_NAME "C:" fmt "\n",	\
	 ## args);
#else /* !DBG_COMMAND_PRINT */
# define PCOMMAND(fmt, args...)
#endif /* DBG_COMMAND_PRINT */


#if defined(DBG_PRINT_SG)
static inline void PRINT_SG(spd_scatterlist_t *sg, int n, int id)
{
  int j;
  for(j=0; j < n; j++){
    printk(KERN_WARNING DBG_SYS_NAME "<spd%c>sg[%4d].addr=%08x cnt=%08x\n",
	   id+'a', j, le32_to_cpu(sg[j].bus_address), le32_to_cpu(sg[j].count));
  }
}
#else /* !DBG_PRINT_SG */
# define PRINT_SG(sg, n, id)
#endif /* DBG_PRINT_SG */


#if defined(DBG_PRINT_BVEC)
static inline void PRINT_BVEC(int i, u32 addr, u32 size, int offset, int n, int rt)
{
  char type;
  if(rt){
    type = 'D';
  } else {
    type = 'N';
  }
  printk(KERN_WARNING DBG_SYS_NAME "(%c)bvec[%3d]addr=%08x size=%08x off=%08x n=%d\n", type, i, addr, size, offset, n);
}
#else /* !DBG_PRINT_BVEC */
# define PRINT_BVEC(i, addr, size, offset, n, rt)
#endif /* DBG_PRINT_BVEC */


#if defined(DBG_DUMP_DATA)
static inline void DUMP_DATA(char *buf, int size)
{
  int i = 0, j = 0, k = 0;
  printk("----------------\n");
  for(i = 0; i < size/16; i++){
    printk("%03X(%03d): ", i*16, i*16);
    for(j = 0; j < 16; j++){
      printk("%02X ", buf[i*16+j]);
      if(j == 7){
	printk(" ");
      }
    }

    printk(" |");
    for(k = 0; k < 16; k++){
      if(0x1F < buf[i*16+k] && buf[i*16+k] < 0x7F){
	printk("%c", buf[i*16+k]);
      } else {
	printk(".");
      }
    }
    printk("|\n");
  }
  printk("----------------\n");
}
#else
# define DUMP_DATA(buf, size)
#endif /* DBG_DUMP_DATA */

#endif /* _DEBUG_H */
