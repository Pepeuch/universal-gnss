// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_s3_execution_policy.h"
#include <assert.h>
int main(void)
{
  for (int role = UG_S3_WORKER_GNSS; role <= UG_S3_WORKER_RADIO; ++role)
  {
    int expected = UG_S3_CPU_UNPINNED;
#if CONFIG_IDF_TARGET_ESP32S3 && UG_S3_CPU_PROFILE > 0
    if (role == UG_S3_WORKER_GNSS || role == UG_S3_WORKER_ROUTER)
      expected = 1;
    else if (UG_S3_CPU_PROFILE == 2)
      expected = 0;
#endif
    assert(ug_s3_worker_core((ug_s3_worker_role_t)role) == expected);
  }
  assert(ug_s3_worker_core((ug_s3_worker_role_t)99) == UG_S3_CPU_UNPINNED);
}
