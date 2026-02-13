#pragma once

#include <Arduino.h>

struct RetrySchedule {
  unsigned long nextAtMs = 0;
  uint8_t attempt = 0;
};

bool retry_due(unsigned long nowMs, unsigned long dueMs);

uint32_t compute_backoff_with_jitter(uint8_t attempt,
                                     uint32_t baseMs,
                                     uint32_t maxMs,
                                     uint32_t jitterMs);

uint32_t schedule_next_retry(RetrySchedule &schedule,
                             unsigned long nowMs,
                             uint32_t baseMs,
                             uint32_t maxMs,
                             uint32_t jitterMs);

void reset_retry(RetrySchedule &schedule, unsigned long nowMs);
