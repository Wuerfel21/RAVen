
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

typedef unsigned int uint;

union r_stereo_sample16 {
    struct {int16_t l,r;};
    struct {int16_t main,sub;};
} typedef r_stereo_sample16;

union r_stereo_sample32 {
    struct {int32_t l,r;};
    struct {int32_t main,sub;};
} typedef r_stereo_sample32;

inline int16_t add16sat(int16_t a,int16_t b) {
    int32_t tmp = (int32_t)a + (int32_t)b;
    if (tmp < INT16_MIN) return INT16_MIN;
    if (tmp > INT16_MAX) return INT16_MAX;
    return tmp;
}

inline int r_clamp(int val,int min,int max) {
    if (val < min) return min;
    if (val > max) return max;
    return val;
}

// Mask and sign extend
inline int r_signx(uint x,uint b) {
    x &= (1U<<b)-1;
    int m = 1U << (b - 1);
    return (x^m)-m;
}


inline r_stereo_sample16 raven_mix16(r_stereo_sample16 a,r_stereo_sample16 b) {
    r_stereo_sample16 rv = {
        .l=add16sat(a.l,b.l),.r=add16sat(a.r,b.r)
    };
    return rv;
}

inline r_stereo_sample16 raven_clampto16(r_stereo_sample32 s) {
    r_stereo_sample16 rv = {
        .l=r_clamp(s.l>>3,INT16_MIN,INT16_MAX),.r=r_clamp(s.r>>3,INT16_MIN,INT16_MAX)
    };
    return rv;
}

inline r_stereo_sample32 raven_expand32(r_stereo_sample16 s) {
    r_stereo_sample32 rv = {
        .l=((int32_t)s.l)<<3,.r=((int32_t)s.r)<<3 // RAVen uses 19 bit internal precision
    };
    return rv;
}

enum {
    RAVEN_RATE = 32000,
    RAVEN_UNITLENGTH = 32,
    RAVEN_UNITS_PER_BLOCK = 19,
    RAVEN_BLOCKLENGTH_CODED = RAVEN_UNITLENGTH*RAVEN_UNITS_PER_BLOCK,
    RAVEN_BLOCKLENGTH = RAVEN_BLOCKLENGTH_CODED + 2,
};

inline uint8_t getnib1(uint8_t b) {
    return b >> 4;
}
inline uint8_t getnib0(uint8_t b) {
    return b & 0xf;
}
inline void setnib1(uint8_t* b,uint nib) {
    *b = (*b&0x0f)+((nib&0x0f)<<4);
}
inline void setnib0(uint8_t* b,uint nib) {
    *b = (*b&0xf0)+((nib&0x0f));
}

enum {
    R_PREDICTOR_DELTA = 0,
    R_PREDICTOR_AVG = 1,
    R_PREDICTOR_SAME = 2,
    R_PREDICTOR_ZERO = 3, // Not very useful?
};

enum {
    R_SUBAPPLY_2BIT  = 0,
    R_SUBAPPLY_FIRST = 1,
    R_SUBAPPLY_LAST  = 2,
    R_SUBAPPLY_TWICE = 3,
};


enum {
    R_ROTATION_LEFT  = 0, // [+1, 0]
    R_ROTATION_RIGHT = 1, // [ 0,+1]
    R_ROTATION_FRONT = 2, // [+1,+1]
    R_ROTATION_BACK  = 3, // [+1,-1]
};

inline int32_t raven_predictor_value(uint predictor, int32_t h1, int32_t h2) {
    switch (predictor) {
        case R_PREDICTOR_DELTA: return (h1<<1) - h2;
        case R_PREDICTOR_AVG:   return (h1+h2)>>1;
        case R_PREDICTOR_SAME:  return h1;
        case R_PREDICTOR_ZERO:  return 0;
        default: __builtin_unreachable();
    }
}

inline void sample_enmatrix(uint rot, int32_t *main_p, int32_t *sub_p) {
    int32_t lv = *main_p,rv = *sub_p;
    switch (rot) {
    case R_ROTATION_LEFT:
        // (NO OP)
        *main_p = lv;
        *sub_p = rv;
        break;
    case R_ROTATION_RIGHT:
        *main_p = rv;
        *sub_p = lv;
        break;
    case R_ROTATION_FRONT:
        *main_p = (lv+rv)>>1;
        *sub_p  = (lv-rv)>>1;
        break;
    case R_ROTATION_BACK:
        *main_p  = (lv-rv)>>1;
        *sub_p = (lv+rv)>>1;
        break;
    }
}

inline void sample_dematrix(uint rot, int32_t *left_p, int32_t *right_p) {
    int32_t mv = *left_p,sv = *right_p;
    switch (rot) {
    case R_ROTATION_LEFT:
        // (NO OP)
        *left_p = mv;
        *right_p = sv;
        break;
    case R_ROTATION_RIGHT:
        *left_p = sv;
        *right_p = mv;
        break;
    case R_ROTATION_FRONT:
        *left_p  = mv+sv;
        *right_p = mv-sv;
        break;
    case R_ROTATION_BACK:
        *left_p  = sv+mv;
        *right_p = sv-mv;
        break;
    }
}

inline void sample_rematrix(uint prev_rot, uint new_rot, int32_t *main_p, int32_t *sub_p) {
    sample_dematrix(prev_rot,main_p,sub_p);
    sample_enmatrix(new_rot,main_p,sub_p);
}

struct raven_unit {
    uint8_t main_coding;
    // Bits 0..3 : ADPCM scale
    // Bits 4..5 : Predictor type
    // Bits 7..8 : Rotation
    uint8_t sub_coding;
    // Bits 0..3 : ADPCM scale
    // Bits 4..5 : Predictor type
    // Bits 7..8 : Apply mode
    uint8_t adpcm_main[RAVEN_UNITLENGTH/2];
    uint8_t adpcm_sub[RAVEN_UNITLENGTH/4];
} __attribute__((packed));

struct raven_block {
    int16_t raw1l,raw1r;
    int16_t raw2l,raw2r;
    uint32_t branch_condition;
    int32_t  branch_offset;
    uint8_t padding[512-(16+sizeof(struct raven_unit)*RAVEN_UNITS_PER_BLOCK)];
    struct raven_unit units[RAVEN_UNITS_PER_BLOCK];
} __attribute__((packed));

