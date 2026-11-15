/* SPDX-License-Identifier: LGPL-3.0-only */
#include "espnow_rtcm_storage.h"
#include <assert.h>
#include <pthread.h>
#include <stdlib.h>
static pthread_mutex_t publication = PTHREAD_MUTEX_INITIALIZER;
static void enter(void)
{
  assert(!pthread_mutex_lock(&publication));
}
static void leave(void)
{
  assert(!pthread_mutex_unlock(&publication));
}
static espnow_rtcm_storage_t slot = {.enter = enter, .exit = leave};
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed = PTHREAD_COND_INITIALIZER;
static bool entered, proceed;
static unsigned calls;
static void* allocate(size_t size)
{
  pthread_mutex_lock(&lock);
  ++calls;
  entered = true;
  pthread_cond_broadcast(&changed);
  while (!proceed)
    pthread_cond_wait(&changed, &lock);
  pthread_mutex_unlock(&lock);
  return malloc(size);
}
static void* reserve(void* arg)
{
  (void)arg;
  assert(espnow_rtcm_storage_reserve(&slot, allocate));
  return NULL;
}
int main(void)
{
  pthread_t task;
  assert(!pthread_create(&task, NULL, reserve, NULL));
  pthread_mutex_lock(&lock);
  while (!entered)
    pthread_cond_wait(&changed, &lock);
  assert(!espnow_rtcm_storage_get(&slot));  // Partially initialized data is invisible.
  assert(!espnow_rtcm_storage_reserve(&slot, allocate) && calls == 1);
  proceed = true;
  pthread_cond_broadcast(&changed);
  pthread_mutex_unlock(&lock);
  assert(!pthread_join(task, NULL));
  espnow_rtcm_storage_data_t* data = espnow_rtcm_storage_get(&slot);
  assert(data && data->queue.count == 0 && !data->message.len);
  assert(!espnow_rtcm_storage_reserve(&slot, allocate) && calls == 1);
  free(data);  // Test-only reboot; there are no remaining readers.
  return 0;
}
