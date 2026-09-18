// main/rid_radio_lock.h —— store 访问锁（UI 与射频任务共享）
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool rid_radio_lock_store(int timeout_ms);
void rid_radio_unlock_store(void);

#ifdef __cplusplus
}
#endif
