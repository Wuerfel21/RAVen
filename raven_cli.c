
#include "raven_decode.h"
#include "raven_encode.h"
#include "raven_statistics.h"
#include <stdio.h>

char *rotnames[] = {
    [R_ROTATION_FRONT] = "FRONT",
    [R_ROTATION_BACK] = "BACK",
    [R_ROTATION_LEFT] = "LEFT",
    [R_ROTATION_RIGHT] = "RIGHT",
};

char *prednames[] = {
    [R_PREDICTOR_DELTA] = "DELTA",
    [R_PREDICTOR_AVG] = "AVG",
    [R_PREDICTOR_SAME] = "SAME",
    [R_PREDICTOR_ZERO] = "ZERO",
};

char *applynames[] = {
    [R_SUBAPPLY_2BIT] = "2BIT",
    [R_SUBAPPLY_FIRST] = "FIRST",
    [R_SUBAPPLY_LAST] = "LAST",
    [R_SUBAPPLY_TWICE] = "TWICE",
};

int main(int argc,char** argv) {

    if (argc < 2) {
        printf("Error: no arguments\n");
        return -1;
    }

    if (!strcmp(argv[1],"encode")) {
        if (argc < 4) {
            printf("Error: not enough arguments\n");
            return -1;
        }
        FILE* in = fopen(argv[2],"rb");
        FILE* out = fopen(argv[3],"wb");

        struct raven_statistic statistic = {};

        for (;;) {
            r_stereo_sample16 buffer[RAVEN_BLOCKLENGTH] = {};
            size_t read = fread(buffer,sizeof(r_stereo_sample16),RAVEN_BLOCKLENGTH,in);
            if (!read) break;
            struct raven_block block = {};
            raven_encode_block(&block,&statistic,buffer);
            fwrite(&block,sizeof(struct raven_block),1,out);
        }

        raven_print_statistic(&statistic);

        fclose(out);
        fclose(in);
    } else if (!strcmp(argv[1],"decode")) {
        if (argc < 4) {
            printf("Error: not enough arguments\n");
            return -1;
        }
        FILE* in = fopen(argv[2],"rb");
        FILE* out = fopen(argv[3],"wb");
        for (;;) {
            struct raven_block block = {};
            size_t read = fread(&block,sizeof(struct raven_block),1,in);
            if (read == 0) break;
            r_stereo_sample16 buffer[RAVEN_BLOCKLENGTH];
            raven_decode_block(&block,buffer);
            fwrite(buffer,sizeof(r_stereo_sample16),RAVEN_BLOCKLENGTH,out);
        }
        fclose(out);
        fclose(in);
    } else if (!strcmp(argv[1],"smp_info")) {
        if (argc < 4) {
            printf("Error: not enough arguments\n");
            return -1;
        }
        FILE* in = fopen(argv[2],"rb");
        int pos = atoi(argv[3]);
        printf("Info about sample %d:\n",pos);
        int blockn = pos / RAVEN_BLOCKLENGTH;
        int blocki = pos % RAVEN_BLOCKLENGTH;
        printf(" - Sample %3d/%3d in block %d\n",blocki,RAVEN_BLOCKLENGTH,blockn);
        fseek(in,blockn*512,SEEK_SET);
        struct raven_block block = {};
        if (!fread(&block,sizeof(struct raven_block),1,in)) {
            printf("Couldn't read block!\n");
            return -1;
        }
        r_stereo_sample16 buffer[RAVEN_BLOCKLENGTH];
        raven_decode_block(&block,buffer);
        r_stereo_sample16 decoded = buffer[blocki];
        printf(" - Decodes to %04X, %04X\n",decoded.l&0xFFFF,decoded.r&0xFFFF);
        if (blocki<2) {
            printf(" - Raw sample!\n");
        } else {
            int un = (blocki-2) / RAVEN_UNITLENGTH;
            int ui = (blocki-2) % RAVEN_UNITLENGTH;
            struct raven_unit *unit = &block.units[un];
            bool mono = unit->sub_coding == 0;
            printf(" - Sample %3d/%3d in unit %2d/%2d\n",ui,RAVEN_UNITLENGTH,un,RAVEN_UNITS_PER_BLOCK);
            printf(" - Unit info:\n");
            printf("   - Mode: %-6s Rotation: %-5s\n",mono?"MONO":"STEREO",rotnames[unit->main_coding>>6]);
            printf("   - MAIN Predictor: %-5s Scale: %2d\n",prednames[(unit->main_coding>>4)&3],unit->main_coding&15);
            if (!mono) {
                printf("   - SUB Predictor: %-5s Scale: %2d\n",prednames[(unit->sub_coding>>4)&3],unit->sub_coding&15);
                printf("   - SUB Apply mode: %-5s\n",applynames[unit->sub_coding>>6]);
            }
            int main_delta = r_signx(unit->adpcm_main[ui>>1]>>((ui&1)*4),4);
            if (mono) main_delta = (main_delta << 2) + ((unit->adpcm_sub[ui>>2]>>((ui&3)*2))&3);
            printf(" - MAIN DELTA: %+3d\n",main_delta);
            if (mono) {
                // Nothing
            } else if ((unit->sub_coding>>6) == R_SUBAPPLY_2BIT) {
                int sub_delta = r_signx((unit->adpcm_sub[ui>>2]>>((ui&3)*2)),2);
                printf(" - SUB DELTA (2BIT): %+3d\n",sub_delta);
            } else {
                int sub_delta = r_signx((unit->adpcm_sub[ui>>2]>>((ui&2)*2)),4);
                printf(" - SUB DELTA (4BIT): %+3d\n",sub_delta);
            }
        }
        
    
    } /*else if (!strcmp(argv[1],"prediction_analyze")) {
        if (argc != 3) {
            printf("Error: not enough arguments\n");
            return -1;
        }
        FILE* in = fopen(argv[2],"rb");

        float *sinckernel = raven_makesinc();
        r_stereo_sample16 buffer[3][RAVEN_BLOCKLENGTH*2] = {};
        r_stereo_sample32 history[2] = {{},{}};
        uint32_t bestcount[512] = {};
        fread(&history[1],sizeof(r_stereo_sample16),1,in);
        fread(&history[0],sizeof(r_stereo_sample16),1,in);
        for (int i=0;;i++) {
            int read = fread(buffer[i%3],sizeof(r_stereo_sample16),RAVEN_BLOCKLENGTH*2,in);
            if (i>=1) {
                r_stereo_sample32 lowbuffer[RAVEN_BLOCKLENGTH];
                raven_downsample(buffer[(i-1)%3],i>=2?buffer[(i-2)%3]:NULL,read?buffer[(i-0)%3]:NULL,RAVEN_BLOCKLENGTH,lowbuffer,sinckernel);
                for (int i=0;i<RAVEN_BLOCKLENGTH;i++) {
                    r_stereo_sample32 s = lowbuffer[i];
                    bestcount[find_best_predictor(s.l,history[0].l,history[1].l)]++;
                    bestcount[find_best_predictor(s.r,history[0].r,history[1].r)]++;
                    history[1] = history[0];
                    history[0] = s;
                }
            }
            if (!read) break;
        }
        free(sinckernel);
        fclose(in);

        for (uint p=0;p<512;p++) {
            printf("%03X : %d\n",p,bestcount[p]);
        }
    } else if (!strcmp(argv[1],"prediction_stats")) {
        if (argc != 3) {
            printf("Error: not enough arguments\n");
            return -1;
        }
        FILE* in = fopen(argv[2],"rb");
        uint32_t predcount[16] = {};
        for (;;) {
            struct raven_block block = {};
            int read = fread(&block,sizeof(struct raven_block),1,in);
            if (read == 0) break;
            for (uint i=0;i<RAVEN_UNITS_PER_BLOCK;i++) {
                predcount[block.units[i].coding >> 4]++;
            }
        }
        fclose(in);

        for (uint i=0;i<16;i++) {
            printf("%02d : %d\n",i,predcount[i]);
        }

    } */else {
        printf("Unknown command\n");
    }
    return 0;
}


