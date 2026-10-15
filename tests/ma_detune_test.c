#include "ma_detune.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
static unsigned checks,failures;
#define CHECK(x) do{++checks;if(!(x)){++failures;printf("FAIL line %u: %s\n",(unsigned)__LINE__,#x);}}while(0)
int main(void)
{
    int16_t *input=malloc(48000*2),*whole=malloc(50000*2),*other=malloc(50000*2),*blocks=malloc(50000*2),*right=malloc(50000*2);
    MaDetune a={0},b={0};unsigned i,n,total=0,crossings=0;double first=0,last=0,frequency,cents;
    if(!input||!whole||!other||!blocks||!right)return 2;
    for(i=0;i<48000;++i)input[i]=(int16_t)(sin(6.283185307179586*440*i/48000)*20000);
    n=ma_detune_pcm(&a,input,input,48000,whole,other,50000);CHECK(n==48000&&memcmp(input,whole,96000)==0);
    ma_detune_set(&a,1);ma_detune_set(&b,1);n=ma_detune_pcm(&a,input,input,48000,whole,other,50000);
    CHECK(n>48340&&n<48355);
    for(i=0;i<48000;i+=96){unsigned count=ma_detune_pcm(&b,input+i,input+i,96,blocks+total,right+total,50000-total);
        CHECK(count>=96&&count<=98);total+=count;}
    CHECK(total==n&&memcmp(whole,blocks,n*2)==0&&memcmp(other,right,n*2)==0);
    for(i=1;i<n;++i)if(whole[i-1]<0&&whole[i]>=0){double at=(i-1)+(double)-whole[i-1]/(whole[i]-whole[i-1]);
        if(!crossings)first=at;last=at;++crossings;}
    frequency=(crossings-1)*48000/(last-first);cents=1200*log(frequency/440)/log(2.0);
    CHECK(cents>-12.6&&cents<-12.4);printf("Detuned 440 Hz: %.5f Hz, %.4f cents\n",frequency,cents);
    ma_detune_set(&b,0);CHECK(!b.primed&&!b.phase);
    CHECK(ma_detune_pcm(&b,input,input,96,whole,other,96)==96&&memcmp(whole,input,192)==0);
    CHECK(ma_detune_pcm(&b,input,input,96,whole,other,95)==0);
    free(input);free(whole);free(other);free(blocks);free(right);
    printf("MA detune resampling: %u checks, %u failures.\n",checks,failures);return failures?1:0;
}
