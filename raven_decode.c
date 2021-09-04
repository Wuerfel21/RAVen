
#include "raven_decode.h"

void raven_decode_block(struct raven_block *block, r_stereo_sample16 *buffer) {

    int32_t mainhistory[2];
    int32_t subhistory[2];
    r_stereo_sample16 raw1 = {.l=block->raw1l,.r=block->raw1r}, raw2 = {.l=block->raw2l,.r=block->raw2r};
    mainhistory[1] = raven_expand32(raw1).main;
    mainhistory[0] = raven_expand32(raw2).main;
    subhistory[1] = raven_expand32(raw1).sub;
    subhistory[0] = raven_expand32(raw2).sub;
    uint prev_rot = R_ROTATION_LEFT;


    buffer[0] = raw1;
    buffer[1] = raw2;

    for (uint ui=0;ui<RAVEN_UNITS_PER_BLOCK;ui++) {
        struct raven_unit *unit = &block->units[ui];
        bool mono = unit->sub_coding == 0;
        uint rot = unit->main_coding>>6;
        uint main_predictor = (unit->main_coding>>4)&3, sub_predictor = (unit->sub_coding>>4)&3;
        uint main_scale = unit->main_coding&15, sub_scale = unit->sub_coding&15;
        uint sub_apply = unit->sub_coding>>6;
        bool twobit = sub_apply == R_SUBAPPLY_2BIT;
        sample_rematrix(prev_rot,rot,&mainhistory[0],&subhistory[0]);
        sample_rematrix(prev_rot,rot,&mainhistory[1],&subhistory[1]);

        for (uint i=0;i<RAVEN_UNITLENGTH;i++) {
            uint sample_idx = 2+ui*RAVEN_UNITLENGTH+i;

            int32_t main_pred = raven_predictor_value(main_predictor,mainhistory[0],mainhistory[1]);
            int main_delta = r_signx(unit->adpcm_main[i>>1] >> ((i&1)*4),4);
            if (mono) main_delta = (main_delta << 2) + ((unit->adpcm_sub[i>>2]>>((i&3)*2))&3);
            mainhistory[1] = mainhistory[0];
            mainhistory[0] = main_pred + (main_delta<<main_scale);
            

            bool odd_sample = i&1;
            if (mono || (sub_apply == R_SUBAPPLY_FIRST && odd_sample) || (sub_apply == R_SUBAPPLY_LAST && !odd_sample)) {
                // Do nothing (history will not advance)
            } else {
                int32_t sub_pred = raven_predictor_value(sub_predictor,subhistory[0],subhistory[1]);
                int sub_delta = twobit ? r_signx((unit->adpcm_sub[i>>2]>>((i&3)*2)),2) : r_signx((unit->adpcm_sub[i>>2]>>((i&2)*2)),4);
                subhistory[1] = subhistory[0];
                subhistory[0] = sub_pred + (sub_delta<<sub_scale);
            }

            r_stereo_sample32 out = {.main=mainhistory[0],.sub=subhistory[0]};
            //if (mono) {out.l =0;out.r=0;}; // DEBUG DEBUG DEBUG
            //out.main = 0;
            sample_dematrix(rot,&out.main,&out.sub);
            buffer[sample_idx] = raven_clampto16(out);
        }
        prev_rot = rot;
    }
}

