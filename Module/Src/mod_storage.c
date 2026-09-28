/* W25Q128 learning journal backend. Sectors 9/10 reserved exclusively.
 * Config sector0, log1..8 and calibration255 are not touched.
 * Mutex covers whole NOR operations; future W25Q users must share this owner. */
#include "mod_storage.h"
#include "drv_w25q128.h"
#include "cmsis_os2.h"
#include <string.h>
static osMutexId_t storage_mutex;
static bool flash_ready;
void mod_storage_init(void) {
 if(!storage_mutex) storage_mutex=osMutexNew(NULL);
 /* Hardware probe is deferred until a task requests battery storage. */
}
static bool acquire(void) {
 if(!storage_mutex||osMutexAcquire(storage_mutex,1000)!=osOK) return false;
 if(!flash_ready) flash_ready=drv_w25q128_init();
 if(!flash_ready||drv_w25q128_read_id()!=BOARD_W25Q128_EXPECTED_ID) {
  flash_ready=false;osMutexRelease(storage_mutex);return false;
 }
 return true;
}
static bool bounds(uint32_t o,size_t n) {return o<=8192u&&n<=8192u-o;}
int mod_storage_battery_read(void *c,uint32_t o,uint8_t *b,size_t n) {
 (void)c;if(!b||!bounds(o,n)||!acquire()) return -1;
 bool ok=drv_w25q128_read(4096u*STORAGE_SEC_BATTERY_START+o,b,(uint32_t)n);
 osMutexRelease(storage_mutex);return ok?0:-1;
}
int mod_storage_battery_program(void *c,uint32_t o,const uint8_t *b,size_t n) {
 uint8_t check[64];(void)c;
 if(!b||n>sizeof(check)||!bounds(o,n)||!acquire()) return -1;
 bool ok=drv_w25q128_write(4096u*STORAGE_SEC_BATTERY_START+o,b,(uint32_t)n);
 if(ok) ok=drv_w25q128_read(4096u*STORAGE_SEC_BATTERY_START+o,check,(uint32_t)n)&&!memcmp(b,check,n);
 osMutexRelease(storage_mutex);return ok?0:-1;
}
int mod_storage_battery_erase(void *c,unsigned sector) {
 uint8_t b[64];bool ok;(void)c;
 if(sector>=STORAGE_SEC_BATTERY_COUNT||!acquire()) return -1;
 ok=drv_w25q128_erase_sector(STORAGE_SEC_BATTERY_START+sector);
 for(unsigned o=0;ok&&o<4096;o+=sizeof(b)) {
  ok=drv_w25q128_read(4096u*(STORAGE_SEC_BATTERY_START+sector)+o,b,sizeof(b));
  for(unsigned j=0;ok&&j<sizeof(b);j++) if(b[j]!=0xFF) ok=false;
 }
 osMutexRelease(storage_mutex);return ok?0:-1;
}
int mod_storage_param_backup_write(const uint8_t *data, size_t len) {
 if(!data||len==0||len>STORAGE_SEC_CONFIG_SIZE||!acquire()) return -1;
 bool ok=drv_w25q128_erase_sector(4096u*STORAGE_SEC_CONFIG);
 if(ok) ok=drv_w25q128_write(4096u*STORAGE_SEC_CONFIG,data,(uint32_t)len);
 osMutexRelease(storage_mutex);return ok?0:-1;
}
int mod_storage_param_backup_read(uint8_t *out, size_t len) {
 if(!out||len==0||len>STORAGE_SEC_CONFIG_SIZE||!acquire()) return -1;
 bool ok=drv_w25q128_read(4096u*STORAGE_SEC_CONFIG,out,(uint32_t)len);
 osMutexRelease(storage_mutex);return ok?0:-1;
}
