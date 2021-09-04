#include "raven_statistics.h"
#include <stdio.h>
#include <math.h>



void raven_add_to_statistic(struct raven_statistic *stat, struct raven_unit *unit, uint64_t error) {
    stat->total++;

    stat->rot[unit->main_coding>>6]++;
    stat->main_scale[unit->main_coding&15]++;
    stat->main_predictor[(unit->main_coding>>4)&3]++;

    if (unit->sub_coding == 0) stat->mono++;
    else {
        stat->sub_apply[unit->sub_coding>>6]++;
        stat->sub_scale[unit->sub_coding&15]++;
        stat->sub_predictor[(unit->sub_coding>>4)&3]++;
    }

    if (__builtin_add_overflow(error,stat->errsum_low,&stat->errsum_low)) stat->errsum_high++;

}

inline double percentage(double v,double total) {
    return (v/total)*100.0;
}

static void print_histogram(uint32_t *scales,uint32_t total) {
    printf("  ");
    for (int i=0;i<16;i++) printf("%5d",i);
    printf("\n  ");
    for (int i=0;i<16;i++) printf("%5.1f",percentage(scales[i],total));
    printf("\n");
}

void raven_print_statistic(struct raven_statistic *stat) {
    if (stat->total==0) {
        printf("ERROR: Empty statistic!\n");
        return;
    }
    uint subtotal = stat->total - stat->mono;
    printf("RAVen encoding statistic for %d total units:\n",stat->total);
    printf("Mono mode: %6.2f%% (%d units)\n",percentage(stat->mono,stat->total),stat->mono);
    if (stat->errsum_low || stat->errsum_high) {
        printf("RMS error: %f\n",sqrt(((double)stat->errsum_low + (double)stat->errsum_high * 18446744073709551616.0) / stat->total));
    }
    printf("Rotations: \n");
    printf("    LEFT : %6.2f%%\n",percentage(stat->rot[R_ROTATION_LEFT ],stat->total));
    printf("    RIGHT: %6.2f%%\n",percentage(stat->rot[R_ROTATION_RIGHT],stat->total));
    printf("    FRONT: %6.2f%%\n",percentage(stat->rot[R_ROTATION_FRONT],stat->total));
    printf("    BACK : %6.2f%%\n",percentage(stat->rot[R_ROTATION_BACK ],stat->total));
    printf("Main Predictors: \n");
    printf("    DELTA: %6.2f%%\n",percentage(stat->main_predictor[R_PREDICTOR_DELTA],stat->total));
    printf("    AVG  : %6.2f%%\n",percentage(stat->main_predictor[R_PREDICTOR_AVG  ],stat->total));
    printf("    SAME : %6.2f%%\n",percentage(stat->main_predictor[R_PREDICTOR_SAME ],stat->total));
    printf("    ZERO : %6.2f%%\n",percentage(stat->main_predictor[R_PREDICTOR_ZERO ],stat->total));
    printf("Main Scales: \n");
    print_histogram(stat->main_scale,stat->total);
    if (subtotal==0) {
        printf("-- ENTIRELY MONO!\n");
    } else {
        printf("Sub Predictors: \n");
        printf("    DELTA: %6.2f%%\n",percentage(stat->sub_predictor[R_PREDICTOR_DELTA],subtotal));
        printf("    AVG  : %6.2f%%\n",percentage(stat->sub_predictor[R_PREDICTOR_AVG  ],subtotal));
        printf("    SAME : %6.2f%%\n",percentage(stat->sub_predictor[R_PREDICTOR_SAME ],subtotal));
        printf("    ZERO : %6.2f%%\n",percentage(stat->sub_predictor[R_PREDICTOR_ZERO ],subtotal));
        printf("Sub Applications: \n");
        printf("    2BIT : %6.2f%%\n",percentage(stat->sub_apply[R_SUBAPPLY_2BIT  ],subtotal));
        printf("    FIRST: %6.2f%%\n",percentage(stat->sub_apply[R_SUBAPPLY_FIRST ],subtotal));
        printf("    LAST : %6.2f%%\n",percentage(stat->sub_apply[R_SUBAPPLY_LAST  ],subtotal));
        printf("    TWICE: %6.2f%%\n",percentage(stat->sub_apply[R_SUBAPPLY_TWICE ],subtotal));
        printf("Sub Scales: \n");
        print_histogram(stat->sub_scale,subtotal);
    }
}
