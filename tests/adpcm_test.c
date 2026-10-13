#include "adpcm.h"
#include <stdio.h>
#include <stdlib.h>
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"ADPCM line %d\n",__LINE__);exit(1);}}while(0)
int main(void){uint32_t step=127;int32_t prediction;
    CHECK(sgh_adpcm_next(0,&step,0)==15&&step==127);
    step=127;CHECK(sgh_adpcm_next(7,&step,0)==236&&step==304);
    step=127;CHECK(sgh_adpcm_next(15,&step,0)==-236&&step==304);
    step=24576;CHECK(sgh_adpcm_next(7,&step,32760)==32767&&step==24576);
    step=24576;CHECK(sgh_adpcm_next(15,&step,-32760)==-32768&&step==24576);
    /* A stream's predictor must survive the RAM ring wrap. The previewed
       opening is the same first sample the continuing decoder would produce. */
    step=1000;prediction=sgh_adpcm_next(2,&step,1234);
    CHECK(prediction==1859&&step==898);
    CHECK(sgh_adpcm_next(10,&step,prediction)==1298&&step==806);
    printf("ADPCM streaming lookahead: %u checks passed.\n",checks);return 0;
}
