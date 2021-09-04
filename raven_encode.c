
#include "raven_encode.h"
#include <math.h>
#include <stdio.h>

inline int32_t divRoundClosest( int32_t A, int32_t B )
{
if(A<0)
    if(B<0)
        return (A + (-B+1)/2) / B + 1;
    else
        return (A + ( B+1)/2) / B - 1;
else
    if(B<0)
        return (A - (-B+1)/2) / B - 1;
    else
        return (A - ( B+1)/2) / B + 1;
}

inline int32_t round_sar(int32_t val,uint sar) {
    return divRoundClosest(val,1u<<sar);
    //return val/(1<<sar);
    //return val>>sar;
}

static int64_t intsquare(int32_t val) {
    if (val > 0x100000) val = 0x100000;
    if (val < -0x100000) val = -0x100000;
    return (int64_t)val * (int64_t)val;
}


void raven_encode_block(struct raven_block *block, struct raven_statistic *statistic,r_stereo_sample16 *inbuffer) {

    block->raw1l = inbuffer[0].l;
    block->raw1r = inbuffer[0].r;
    block->raw2l = inbuffer[1].l;
    block->raw2r = inbuffer[1].r;
    int32_t mainhist[2] = {block->raw2l<<3, block->raw1l<<3};
    int32_t subhist[2]  = {block->raw2r<<3, block->raw1r<<3};
    //uint64_t dither_rngstate1 = inbuffer[0].l + inbuffer[1].r + 0x80000000; // Init state from first samples to avoid repetitive dither noise
    //uint64_t dither_rngstate2 = inbuffer[2].l + inbuffer[3].r + 0x8000BEEF;
    uint prev_rot = R_ROTATION_LEFT;

    for (uint ui=0;ui<RAVEN_UNITS_PER_BLOCK;ui++) {
        uint unitbase = 2 + ui*RAVEN_UNITLENGTH;

        //uint64_t dither_rngstate_new1,dither_rngstate_new2;

        // Try encoding in all possible modes
        // (4 rotations)*((64 main modes)+(256 sub modes))
        
        int64_t best_roterror = INT64_MAX, best_roterror_real = INT64_MAX;
        int best_rot = -1;
        int32_t best_rot_mainhist[2];
        int32_t best_rot_subhist[2];
        for (uint rot=0;rot<4;rot++) {
            int32_t mainhist_tmpl[2] = {mainhist[0],mainhist[1]};
            int32_t subhist_tmpl[2] = {subhist[0],subhist[1]};
            sample_rematrix(prev_rot,rot,&mainhist_tmpl[0],&subhist_tmpl[0]);
            sample_rematrix(prev_rot,rot,&mainhist_tmpl[1],&subhist_tmpl[1]);

            r_stereo_sample32 rematrixed_input[RAVEN_UNITLENGTH];
            for (uint i=0;i<RAVEN_UNITLENGTH;i++) {
                rematrixed_input[i] = raven_expand32(inbuffer[unitbase+i]);
                sample_enmatrix(rot,&rematrixed_input[i].main,&rematrixed_input[i].sub);
            }

            int64_t monoerror = 0; // Error incurred by not coding the sub channel (stays at current level)
            for (uint i=0;i<RAVEN_UNITLENGTH;i++) monoerror += intsquare(rematrixed_input[i].sub - subhist_tmpl[0]);


            int8_t best_maindata[RAVEN_UNITLENGTH];
            int32_t best_mainhist[2];
            int64_t best_mainerror = INT64_MAX;
            int best_mainmode = -1;
            for (int m=0;m<128;m++) {
                bool mono = m&64;
                uint scale = m&15;
                uint predictor = (m>>4)&3;
                int32_t mainhist_new[2] = {mainhist_tmpl[0],mainhist_tmpl[1]};
                int8_t maindata_new[RAVEN_UNITLENGTH];

                int64_t merror = mono ? monoerror : 0;

                for(uint i=0;i<RAVEN_UNITLENGTH;i++) {
                    int32_t prediction = raven_predictor_value(predictor,mainhist_new[0],mainhist_new[1]);
                    int32_t target = rematrixed_input[i].main;
                    int32_t diff = target - prediction;
                    int32_t scaled_diff = round_sar(diff,scale);
                    int32_t quantized_diff = mono ? r_clamp(scaled_diff,-32,31) : r_clamp(scaled_diff,-8,7);
                    maindata_new[i] = quantized_diff;
                    mainhist_new[1] = mainhist_new[0];
                    mainhist_new[0] = prediction + (quantized_diff<<scale);
                    merror += intsquare(mainhist_new[0]-rematrixed_input[i].main);
                }

                if (merror < best_mainerror) {
                    best_mainerror = merror;
                    best_mainmode = m;
                    memcpy(best_maindata,maindata_new,sizeof(int8_t)*RAVEN_UNITLENGTH);
                    memcpy(best_mainhist,mainhist_new,sizeof(int32_t)*2);
                }
            }

            if (best_mainmode < 0) printf("Oh no, couldn't code main????\n");
            if (best_mainerror < 0) printf("Warning: main error overflow???\n");
            
            // Encode sub channel only if best main encoding isn't in mono mode
            int8_t best_subdata[RAVEN_UNITLENGTH];
            int32_t best_subhist[2];
            int64_t best_suberror = INT64_MAX;
            int best_submode = -1;
            if (!(best_mainmode&64)) {
                for (int m=1;m<256;m++) { // Mode 0 is not valid
                    uint apply = (m>>6)&3;
                    bool twobit = apply == R_SUBAPPLY_2BIT;
                    uint scale = m&15;
                    uint predictor = (m>>4)&3;
                    int32_t subhist_new[2] = {subhist_tmpl[0],subhist_tmpl[1]};
                    int8_t subdata_new[RAVEN_UNITLENGTH];

                    int64_t merror = 0;

                    for(uint i=0;i<(twobit?RAVEN_UNITLENGTH:RAVEN_UNITLENGTH/2);i++) {
                        int32_t prediction = raven_predictor_value(predictor,subhist_new[0],subhist_new[1]);
                        int32_t diff;
                        
                        switch (apply) {
                        case R_SUBAPPLY_2BIT: {
                            int32_t target = rematrixed_input[i].sub;
                            diff = target - prediction;
                        } break;
                        case R_SUBAPPLY_FIRST: {
                            int32_t target = (rematrixed_input[i*2].sub + rematrixed_input[i*2+1].sub)/2;
                            diff = target - prediction;
                        } break;
                        case R_SUBAPPLY_LAST: {
                            int32_t target = rematrixed_input[i*2+1].sub;
                            diff = target - prediction;
                        }
                        case R_SUBAPPLY_TWICE: {
                            int32_t target1 = rematrixed_input[i*2].sub;
                            int32_t target2 = rematrixed_input[i*2+1].sub;
                            int32_t diff1 = target1-prediction;
                            int32_t diff2 = target2-prediction;
                            switch (predictor) {
                            case R_PREDICTOR_DELTA: 
                                diff = (diff2 + diff1)/4;
                                break;
                            case R_PREDICTOR_AVG:
                                diff = (diff2*2 + diff1)/4;
                                break;
                            case R_PREDICTOR_SAME:
                                diff = (diff2 + diff1)/3;
                                break;
                            case R_PREDICTOR_ZERO:
                                diff = (target1+target2)/2;
                                break;
                            }
                        } break;
                        }

                        /*
                        switch (apply) {
                            case R_SUBAPPLY_2BIT: samplen = i; break;
                            case R_SUBAPPLY_FIRST: samplen = i*2; break;
                            case R_SUBAPPLY_LAST: samplen = i*2+1; break;
                            case R_SUBAPPLY_TWICE: samplen = i*2+1; break;
                        }
                        int32_t target = rematrixed_input[samplen].sub;
                        int32_t diff = target - prediction;
                        if (apply == R_SUBAPPLY_TWICE) { // Needs special care
                            int32_t target2 = rematrixed_input[samplen+1].sub;
                            int32_t diff2;
                            switch(predictor) {
                            case R_PREDICTOR_ZERO:
                                diff2 = 
                                break;
                            case R_PREDICTOR_SAME: return divRoundClosest(diff,2);
                            case R_PREDICTOR_DELTA: return divRoundClosest(diff,3);
                            case R_PREDICTOR_AVG: return divRoundClosest(diff*2,3);
                            default: __builtin_unreachable();
                            }
                        }*/

                        int32_t scaled_diff = round_sar(diff,scale);
                        int32_t quantized_diff = twobit ? r_clamp(scaled_diff,-2,1) : r_clamp(scaled_diff,-8,7);
                        subdata_new[i] = quantized_diff;
                        subhist_new[1] = subhist_new[0];
                        subhist_new[0] = prediction + (quantized_diff<<scale);
                        if (apply == R_SUBAPPLY_TWICE) {
                            int32_t prediction = raven_predictor_value(predictor,subhist_new[0],subhist_new[1]);
                            subhist_new[1] = subhist_new[0];
                            subhist_new[0] = prediction + (quantized_diff<<scale);
                        }
                        switch (apply) {
                        case R_SUBAPPLY_2BIT:
                            merror += intsquare(subhist_new[0]-rematrixed_input[i].sub);
                            break;
                        case R_SUBAPPLY_FIRST:
                            merror += intsquare(subhist_new[0]-rematrixed_input[i*2].sub);
                            merror += intsquare(subhist_new[0]-rematrixed_input[i*2+1].sub);
                            break;
                        case R_SUBAPPLY_LAST:
                            merror += intsquare(subhist_new[1]-rematrixed_input[i*2].sub);
                            merror += intsquare(subhist_new[0]-rematrixed_input[i*2+1].sub);
                            break;
                        case R_SUBAPPLY_TWICE:
                            merror += intsquare(subhist_new[1]-rematrixed_input[i*2].sub);
                            merror += intsquare(subhist_new[0]-rematrixed_input[i*2+1].sub);
                            break;
                        }
                    }

                    if (merror < best_suberror) {
                        best_suberror = merror;
                        best_submode = m;
                        memcpy(best_subdata,subdata_new,sizeof(int8_t)*RAVEN_UNITLENGTH);
                        memcpy(best_subhist,subhist_new,sizeof(int32_t)*2);
                    }
                }
                if (best_submode < 0) printf("Oh no, couldn't code sub????\n");
                if (best_suberror < 0) printf("Warning: sub error overflow???\n");
            } else {
                best_suberror = 0; // Already in mainerror
                best_submode = 0;
                memcpy(best_subhist,subhist_tmpl,sizeof(int32_t)*2);
            }

            int64_t combined_error = best_mainerror+best_suberror;
            int64_t effective_error = combined_error;
            if (rot == prev_rot) effective_error += effective_error>>3;
            if (effective_error < best_roterror /*&& rot == R_ROTATION_FRONT*/ /*&& rot == (ui&3)*/) { // DEBUG DEBUG DEBUG
                // If we're good, fill in structure
                best_rot = rot;
                best_roterror = effective_error;
                best_roterror_real = combined_error;
                memcpy(best_rot_mainhist,best_mainhist,sizeof(int32_t)*2);
                memcpy(best_rot_subhist,best_subhist,sizeof(int32_t)*2);
                block->units[ui].main_coding = (best_mainmode&63) + (rot<<6);
                block->units[ui].sub_coding = best_submode;
                if (best_mainmode&64) { // mono
                    for (uint i=0;i<RAVEN_UNITLENGTH;i++) {
                        uint8_t *mptr = &block->units[ui].adpcm_main[i>>1];
                        uint8_t *sptr = &block->units[ui].adpcm_sub[i>>2];
                        int8_t mdelta = best_maindata[i]>>2;
                        int8_t sdelta = best_maindata[i]&3;
                        *mptr &= ~(15<<((i&1)*4));
                        *mptr |= (mdelta&15)<<((i&1)*4);
                        *sptr &= ~(3<<((i&3)*2));
                        *sptr |= (sdelta&3)<<((i&3)*2);
                    }
                } else { // Not mono
                    for (uint i=0;i<RAVEN_UNITLENGTH;i++) {
                        uint8_t *mptr = &block->units[ui].adpcm_main[i>>1];
                        int8_t mdelta = best_maindata[i];
                        *mptr &= ~(15<<((i&1)*4));
                        *mptr |= (mdelta&15)<<((i&1)*4);
                    }
                    if ((best_submode>>6)==R_SUBAPPLY_2BIT) { // 2 bit mode
                        for (uint i=0;i<RAVEN_UNITLENGTH;i++) {
                            uint8_t *sptr = &block->units[ui].adpcm_sub[i>>2];
                            int8_t sdelta = best_subdata[i];
                            *sptr &= ~(3<<((i&3)*2));
                            *sptr |= (sdelta&3)<<((i&3)*2);
                        }
                    } else { // 4 bit modes
                        for (uint i=0;i<RAVEN_UNITLENGTH/2;i++) {
                            uint8_t *sptr = &block->units[ui].adpcm_sub[i>>1];
                            int8_t sdelta = best_subdata[i];
                            *sptr &= ~(15<<((i&1)*4));
                            *sptr |= (sdelta&15)<<((i&1)*4);
                        }
                    }
                }
            }
        }
        if (best_rot < 0) printf("Just what the shit???\n");
        prev_rot = best_rot;
        memcpy(mainhist,best_rot_mainhist,sizeof(int32_t)*2);
        memcpy(subhist,best_rot_subhist,sizeof(int32_t)*2);
        if (statistic) raven_add_to_statistic(statistic,&block->units[ui],best_roterror_real);
    }

}


