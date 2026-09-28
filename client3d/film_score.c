#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static void word(FILE *file,uint32_t value,unsigned bytes)
{
    for(unsigned i=0;i<bytes;++i)
        if(fputc((int)((value>>(8*i))&255),file)==EOF){perror("score write");exit(1);}
}

int main(int argc,char **argv)
{
    if(argc!=3){fprintf(stderr,"film-score OUTPUT.wav SECONDS\n");return 2;}
    char *end;
    errno=0;
    double seconds=strtod(argv[2],&end);
    if(errno || *end || seconds<=0 || seconds*48000*4>UINT32_MAX-36){fprintf(stderr,"Invalid WAV duration\n");return 2;}
    uint32_t count=(uint32_t)llround(seconds*48000);
    FILE *file=fopen(argv[1],"wb");
    if(!file){perror("score open");return 1;}
    if(fwrite("RIFF",1,4,file)!=4)return 1;
    word(file,36+count*4,4);
    if(fwrite("WAVEfmt ",1,8,file)!=8)return 1;
    word(file,16,4);word(file,1,2);word(file,2,2);word(file,48000,4);word(file,192000,4);word(file,4,2);word(file,16,2);
    if(fwrite("data",1,4,file)!=4)return 1;
    word(file,count*4,4);
    const double tau=6.283185307179586,beat=60.0/144;
    const int root[]={28,24,31,26};
    const int riff[]={0,0,12,0,7,0,10,7,0,12,0,7,10,7,3,7};
    uint32_t random=0x5a1da7;
    double bass_phase=0,pulse_phase=0,previous_noise=0;
    for(uint32_t i=0;i<count;++i) {
        double t=(double)i/48000,b=t/beat;
        int bar=(int)(b/4),step=(int)(b*4),chord=root[(bar/4)%4];
        double beat_time=fmod(t,beat),sixteenth=fmod(t,beat/4),eighth=fmod(t,beat/2);
        double intro=fmin(1,t/3),outro=fmin(1,fmax(0,(seconds-t)/3));
        random^=random<<13;random^=random>>17;random^=random<<5;
        double noise=(double)(random>>8)/8388608-1,bright=noise-previous_noise;
        previous_noise=noise;
        double kick=sin(tau*(49*beat_time+34*.028*(1-exp(-beat_time/.028))))*exp(-beat_time*18);
        double snare_time=fmod(t,beat*2)-beat;
        double snare=snare_time>=0?(bright*.44+sin(tau*183*snare_time)*.23)*exp(-snare_time*22):0;
        double hat=bright*exp(-sixteenth*140)*((step%4==2)?.17:.075);
        double fill=bar%4==3 && (int)b%4==3?bright*.12*exp(-sixteenth*38):0;
        double frequency=440*pow(2,(chord-69)/12.0);
        bass_phase+=frequency/48000;
        double bass_envelope=(1-exp(-eighth*95))*exp(-eighth*5.5)*(1-.62*exp(-beat_time*24));
        double bass=tanh((sin(tau*bass_phase)+.4*sin(tau*bass_phase*2)+.18*sin(tau*bass_phase*3))*2)*bass_envelope;
        int note=chord+24+riff[step%16];
        pulse_phase+=440*pow(2,(note-69)/12.0)/48000;
        double pulse=(sin(tau*pulse_phase)+.22*sin(tau*pulse_phase*2))*exp(-sixteenth*32);
        double pad=(sin(tau*frequency*4*t)+sin(tau*frequency*4*pow(2,3.0/12)*t)+sin(tau*frequency*6*t))/3;
        double music=(kick*.38+snare*.35+hat+fill+bass*.2)*intro;
        double stereo=.18*sin(tau*.21*t);
        double high=pad*.025+pulse*.045*fmin(1,fmax(0,(t-5)/6));
        double left=tanh((music+high*(1+stereo))*.95)*outro;
        double right=tanh((music+high*(1-stereo))*.95)*outro;
        word(file,(uint16_t)(int16_t)lrint(left*26000),2);
        word(file,(uint16_t)(int16_t)lrint(right*26000),2);
    }
    if(fclose(file)){perror("score close");return 1;}
    return 0;
}
