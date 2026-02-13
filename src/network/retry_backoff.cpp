#include "retry_backoff.h"

bool retry_due(unsigned long nowMs, unsigned long dueMs) {
  return static_cast<long>(nowMs - dueMs) >= 0;
}

uint32_t compute_backoff_with_jitter(uint8_t attempt,
                                     uint32_t baseMs,
                                     uint32_t maxMs,
                                     uint32_t jitterMs) {
  uint32_t waitMs = baseMs;
  uint8_t cappedAttempt = attempt > 8 ? 8 : attempt;

  for (uint8_t i = 0; i < cappedAttempt; i++) {
    if (waitMs >= (maxMs / 2)) {
      waitMs = maxMs;
      break;
    }
    waitMs *= 2;
  }

  if (waitMs > maxMs) waitMs = maxMs;

  uint32_t jitter = random(jitterMs + 1);
  if (waitMs > maxMs - jitter) return maxMs;
  return waitMs + jitter;
}

uint32_t schedule_next_retry(RetrySchedule &schedule,
                             unsigned long nowMs,
                             uint32_t baseMs,
                             uint32_t maxMs,
                             uint32_t jitterMs) {
  uint32_t waitMs = compute_backoff_with_jitter(schedule.attempt, baseMs, maxMs, jitterMs);
  schedule.attempt++;
  schedule.nextAtMs = nowMs + waitMs;
  return waitMs;
}

void reset_retry(RetrySchedule &schedule, unsigned long nowMs) {
  schedule.attempt = 0;
  schedule.nextAtMs = nowMs;
}
