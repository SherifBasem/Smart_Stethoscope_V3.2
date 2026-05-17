/**
 * @file    task_timing.h
 * @brief   Common timing stats structure for RTOS tasks
 */

#ifndef TASK_TIMING_H
#define TASK_TIMING_H

#include <stdint.h>

typedef struct {
    uint32_t wcet_us;          /**< Worst-case execution time (us) */
    uint32_t target_period_us; /**< Expected period (us) */
    uint32_t max_jitter_us;    /**< Max |actual-target| (us) */
    uint32_t missed_deadlines; /**< Count of periods exceeding target */
} TaskTimingStats_t;

#endif /* TASK_TIMING_H */
