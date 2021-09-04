#pragma once
#include "raven_common.h"

struct raven_statistic {
    uint32_t main_scale[16];
    uint32_t sub_scale[16];
    uint32_t main_predictor[4];
    uint32_t sub_predictor[4];
    uint32_t rot[4];
    uint32_t sub_apply[4];
    uint64_t errsum_low,errsum_high;
    uint32_t mono;
    uint32_t total;
};

void raven_add_to_statistic(struct raven_statistic *stat, struct raven_unit *unit, uint64_t error);

void raven_print_statistic(struct raven_statistic *stat);
