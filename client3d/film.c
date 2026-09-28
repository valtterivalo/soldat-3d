#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#define GL_SILENCE_DEPRECATION
#include "game.h"
#include "world.h"
#include "pose.h"
#include "ragdoll.h"
#include "camera.h"
#include "showcase.h"
#include "gostek.h"
#include "map_visuals.h"
#include "effects.h"
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __APPLE__
#include <OpenGL/OpenGL.h>
#include <dlfcn.h>
#endif

typedef enum { SHOT_SHOULDER, SHOT_CHASE, SHOT_ORBIT, SHOT_DUEL, SHOT_WIDE } Shot;
typedef enum { TRACK_FOLLOW, TRACK_FIXED } Tracking;
typedef struct { float *samples; unsigned frames; } Clip;
static const char *const shot_files[WEAPON_COUNT]={
    "deserteagle-fire","mp5-fire","ak74-fire","steyraug-fire","spas12-fire","ruger77-fire",
    "m79-fire","barretm82-fire","m249-fire","minigun-fire","colt1911-fire","slash",
    "chainsaw-r","law","bow-fire","bow-fire","flamer","m2fire",NULL,
    "grenade-throw","grenade-throw",NULL,"throwgun"
};

static void require(int condition,const char *message)
{
    if(condition)return;
    fprintf(stderr,"Film: %s\n",message);
    exit(EXIT_FAILURE);
}

static unsigned number(const char *text)
{
    char *end;
    errno=0;
    unsigned long value=strtoul(text,&end,0);
    require(*text && *text!='-' && !*end && !errno && value<=UINT32_MAX,"invalid unsigned argument");
    return (unsigned)value;
}

static float decimal(const char *text)
{
    char *end;
    errno=0;
    float value=strtof(text,&end);
    require(*text && !*end && !errno,"invalid decimal argument");
    return value;
}

static Clip clip_load(const char *name)
{
    Wave wave=LoadWave(TextFormat("%s/sfx/%s.wav",SOLDAT_ASSET_DIR,name));
    require(IsWaveValid(wave),name);
    WaveFormat(&wave,48000,32,2);
    Clip clip={LoadWaveSamples(wave),wave.frameCount};
    require(clip.samples!=NULL,"sound decode failed");
    UnloadWave(wave);
    return clip;
}

#ifdef __APPLE__
static void *gl_proc(const char *name) {return dlsym(RTLD_DEFAULT,name);}
#endif

int main(int argc,char **argv)
{
    const char *map="Arena2",*preset="match",*output=NULL,*audio_path=NULL,*event_path=NULL;
    const char *card=NULL;
    unsigned seed=0x501da7,start=0,frames=300,width=1920,height=1080,supersample=2;
    int actor_index=0,target_index=-1,population=8;
    float speed=1,angle=0,lens=0;
    Shot shot=SHOT_SHOULDER;
    Tracking tracking=TRACK_FOLLOW;
    for(int arg=1;arg<argc;++arg) {
        if(!strcmp(argv[arg],"--help")) {
            puts("soldat3d-film --output CLIP.mp4 --audio CLIP.wav --events CLIP.jsonl --map NAME --preset match|crossfire|rifles|marksmen|ascent --seed N --population N --actor N --target N --camera shoulder|chase|orbit|duel|wide --tracking follow|fixed --angle DEGREES --fov DEGREES --start TICK --frames N --speed RATIO --width N --height N --supersample N --card intro|outro");
            return 0;
        }
        require(arg+1<argc,"option requires a value");
        const char *option=argv[arg],*value=argv[++arg];
        if(!strcmp(option,"--map"))map=value;
        else if(!strcmp(option,"--preset"))preset=value;
        else if(!strcmp(option,"--output"))output=value;
        else if(!strcmp(option,"--audio"))audio_path=value;
        else if(!strcmp(option,"--events"))event_path=value;
        else if(!strcmp(option,"--card"))card=value;
        else if(!strcmp(option,"--seed"))seed=number(value);
        else if(!strcmp(option,"--start"))start=number(value);
        else if(!strcmp(option,"--frames"))frames=number(value);
        else if(!strcmp(option,"--width"))width=number(value);
        else if(!strcmp(option,"--height"))height=number(value);
        else if(!strcmp(option,"--supersample"))supersample=number(value);
        else if(!strcmp(option,"--population"))population=(int)number(value);
        else if(!strcmp(option,"--actor"))actor_index=(int)number(value);
        else if(!strcmp(option,"--target"))target_index=(int)number(value);
        else if(!strcmp(option,"--speed"))speed=decimal(value);
        else if(!strcmp(option,"--angle"))angle=decimal(value)*DEG2RAD;
        else if(!strcmp(option,"--fov")){lens=decimal(value);require(lens>0 && lens<180,"lens field of view must be between zero and180 degrees");}
        else if(!strcmp(option,"--tracking")) {
            require(!strcmp(value,"follow") || !strcmp(value,"fixed"),"tracking must be follow or fixed");
            tracking=!strcmp(value,"fixed")?TRACK_FIXED:TRACK_FOLLOW;
        }
        else if(!strcmp(option,"--camera")) {
            const char *const names[]={"shoulder","chase","orbit","duel","wide"};
            int found=0;
            for(int i=0;i<=SHOT_WIDE;++i)if(!strcmp(value,names[i])){shot=(Shot)i;found=1;}
            require(found,"unknown camera");
        } else {fprintf(stderr,"Unknown option %s\n",option);return 1;}
    }
    require(output && frames && width && height && supersample && seed,"output, positive dimensions, frames and seed are required");
    require(population>=2 && population<=ACTOR_COUNT && actor_index>=0 && actor_index<population && target_index<population,"invalid actor population or camera subject");
    require(speed>0,"capture speed must be positive");
    require((uint64_t)width*supersample<=INT_MAX && (uint64_t)height*supersample<=INT_MAX,"capture dimensions exceed integer API range");
    SetTraceLogLevel(LOG_WARNING);
    size_t map_index=world_map_index(map);
    require(map_index!=SIZE_MAX,"unknown map");
    world_load(map_index);poses_init();ragdolls_init();
    Game game;
    showcase_init(&game,preset,seed,population);
    Actor before[ACTOR_COUNT];
    uint32_t held[ACTOR_COUNT]={0};
    Input inputs[ACTOR_COUNT];
    for(unsigned tick=0;tick<start;++tick) {
        for(int i=0;i<ACTOR_COUNT;++i) {
            inputs[i]=bot_input(&game,i);
            inputs[i].pressed=inputs[i].held&~held[i];
            held[i]=inputs[i].held;
        }
        game_step(&game,inputs);
    }
    memcpy(before,game.actors,sizeof(before));

#ifdef __APPLE__
    CGLPixelFormatAttribute attributes[]={kCGLPFAOpenGLProfile,(CGLPixelFormatAttribute)kCGLOGLPVersion_3_2_Core,
        kCGLPFAAccelerated,kCGLPFAAllowOfflineRenderers,(CGLPixelFormatAttribute)0};
    CGLPixelFormatObj pixel_format;
    CGLContextObj context;
    GLint formats;
    CGLError error=CGLChoosePixelFormat(attributes,&pixel_format,&formats);
    require(error==kCGLNoError && formats>0,"offscreen pixel format creation failed");
    error=CGLCreateContext(pixel_format,NULL,&context);CGLDestroyPixelFormat(pixel_format);
    require(error==kCGLNoError,"offscreen context creation failed");
    require(CGLSetCurrentContext(context)==kCGLNoError,"offscreen context activation failed");
    void *framework=dlopen("/System/Library/Frameworks/OpenGL.framework/OpenGL",RTLD_LAZY|RTLD_GLOBAL);
    require(framework!=NULL,"OpenGL framework load failed");
    rlLoadExtensions((void *)gl_proc);
    rlglInit((int)(width*supersample),(int)(height*supersample));
    SetShapesTexture((Texture2D){rlGetTextureIdDefault(),1,1,1,PIXELFORMAT_UNCOMPRESSED_R8G8B8A8},(Rectangle){0,0,1,1});
#else
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow((int)(width*supersample),(int)(height*supersample),"Soldat film renderer");
    require(IsWindowReady(),"capture window creation failed");
#endif
    rlSetClipPlanes(.2,2*length(sub(world_bounds.max,world_bounds.min))+1000);
    map_visuals_init();gostek_init();
    Effects effects=effects_load();
    RenderTexture target=LoadRenderTexture((int)(width*supersample),(int)(height*supersample));
    require(IsRenderTextureValid(target),"capture framebuffer creation failed");
    Font font={0};
    if(card) {
        font=LoadFontEx(TextFormat("%s/interface-gfx/play-regular.ttf",SOLDAT_ASSET_DIR),192,NULL,0);
        require(IsFontValid(font),"title font load failed");
        SetTextureFilter(font.texture,TEXTURE_FILTER_BILINEAR);
    }
    Clip sounds[WEAPON_COUNT]={0};
    Clip explosion={0},rocket={0},jets={0};
    size_t sample_count=(size_t)frames*800;
    float *audio=audio_path?calloc(sample_count*2,sizeof(float)):NULL;
    if(audio_path) {
        require(audio!=NULL,"audio allocation failed");
        for(int i=0;i<WEAPON_COUNT;++i)if(shot_files[i])sounds[i]=clip_load(shot_files[i]);
        explosion=clip_load("grenade-explosion");rocket=clip_load("m79-explosion");jets=clip_load("rocketz");
    }
    FILE *events=event_path?fopen(event_path,"w"):NULL;
    if(event_path)require(events!=NULL,"event log creation failed");
    if(events)fprintf(events,"{\"kind\":\"capture\",\"map\":\"%s\",\"preset\":\"%s\",\"seed\":%u,\"start\":%u,\"frames\":%u,\"speed\":%.6f,\"actor\":%d}\n",map,preset,seed,start,frames,speed,actor_index);
    int descriptors[2];
    require(pipe(descriptors)==0,"encoder pipe creation failed");
    pid_t encoder=fork();
    require(encoder>=0,"encoder process creation failed");
    if(encoder==0) {
        if(dup2(descriptors[0],STDIN_FILENO)<0)_exit(126);
        close(descriptors[0]);close(descriptors[1]);
        char dimensions[64],filter[128];
        snprintf(dimensions,sizeof(dimensions),"%ux%u",width*supersample,height*supersample);
        snprintf(filter,sizeof(filter),"scale=%u:%u:flags=lanczos,format=yuv420p",width,height);
        execlp("ffmpeg","ffmpeg","-hide_banner","-loglevel","error","-y","-f","rawvideo","-pixel_format","rgba",
            "-video_size",dimensions,"-framerate","60","-i","pipe:0","-vf",filter,"-c:v","libx264","-preset","fast",
            "-crf","16","-movflags","+faststart","-an",output,(char *)NULL);
        perror("ffmpeg");_exit(127);
    }
    require(close(descriptors[0])==0,"close encoder read descriptor");
    FILE *video=fdopen(descriptors[1],"wb");
    require(video!=NULL,"encoder stream creation failed");
    CameraRig rig={48};
    Camera3D camera={.up={0,1,0},.fovy=62,.projection=CAMERA_PERSPECTIVE};
    Vec3 camera_position=v3(0,0,0),camera_target=v3(0,0,0);
    unsigned kills=0;
    for(unsigned frame=0;frame<frames;++frame) {
        double timeline=start+(double)frame*speed;
        while((double)game.tick<=timeline) {
            memcpy(before,game.actors,sizeof(before));
            for(int i=0;i<ACTOR_COUNT;++i) {
                inputs[i]=bot_input(&game,i);
                inputs[i].pressed=inputs[i].held&~held[i];
                held[i]=inputs[i].held;
            }
            game_step(&game,inputs);
            effects_step(&effects,&game,game.tick);
            for(size_t e=0;e<game.event_count;++e) {
                GameEvent event=game.events[e];
                if(event.kind==EVENT_SHOT)gostek_fire(event.actor,game.tick);
                if(event.kind==EVENT_KILL)++kills;
                if(events)fprintf(events,"{\"kind\":%d,\"frame\":%u,\"tick\":%" PRIu64 ",\"actor\":%d,\"target\":%d,\"weapon\":%d,\"position\":[%.4f,%.4f,%.4f]}\n",event.kind,frame,game.tick,event.actor,event.target,event.weapon,event.position.x,event.position.y,event.position.z);
                if(!audio)continue;
                Clip sound={0};
                if(event.kind==EVENT_SHOT)sound=sounds[event.weapon];
                else if(event.kind==EVENT_EXPLOSION)sound=event.weapon==FRAGGRENADE?explosion:rocket;
                if(!sound.samples)continue;
                Vec3 delta=sub(event.position,game.actors[actor_index].position);
                float distance=length(delta),gain=.62f/(1+distance/210);
                Vec3 right=v3(-cosf(game.actors[actor_index].yaw),0,sinf(game.actors[actor_index].yaw));
                float pan=distance>1?dot(scale(delta,1/distance),right)*.65f:0;
                if(event.actor==actor_index)gain=.85f;
                size_t at=(size_t)frame*800;
                for(size_t i=0;i<sound.frames && at+i<sample_count;++i) {
                    audio[2*(at+i)]+=sound.samples[2*i]*gain*sqrtf((1-pan)*.5f);
                    audio[2*(at+i)+1]+=sound.samples[2*i+1]*gain*sqrtf((1+pan)*.5f);
                }
            }
        }
        float alpha=(float)(timeline-floor(timeline));
        Game scene=game;
        Vec3 bones[ACTOR_COUNT][21];
        for(int i=0;i<ACTOR_COUNT;++i) {
            Actor *actor=&scene.actors[i];
            actor_pose_between(&before[i],&game.actors[i],alpha,bones[i]);
            if(before[i].spawn_id!=actor->spawn_id)continue;
            actor->position=add(actor->previous,scale(sub(actor->position,actor->previous),alpha));
            actor->previous=actor->position;
            actor->yaw=before[i].yaw+atan2f(sinf(actor->yaw-before[i].yaw),cosf(actor->yaw-before[i].yaw))*alpha;
            actor->pitch=before[i].pitch+(actor->pitch-before[i].pitch)*alpha;
        }
        Actor *subject=&scene.actors[actor_index];
        Vec3 center=add(subject->position,v3(0,10,0));
        Vec3 forward=direction(subject->yaw,0),right=v3(-cosf(subject->yaw),0,sinf(subject->yaw));
        Vec3 wanted,look;
        float body_visibility=1;
        if(shot==SHOT_SHOULDER) {
            ShoulderView view=camera_view(&rig,subject,subject->yaw,subject->pitch,1,1,lens>0?lens:64,1.0f/60);
            wanted=view.position;look=view.target;camera.fovy=view.fov;body_visibility=view.body_visibility;
        } else if(shot==SHOT_CHASE) {
            wanted=add(center,add(scale(forward,-84),add(scale(right,26),v3(0,23,0))));
            look=add(center,scale(direction(subject->yaw,subject->pitch),55));camera.fovy=59;
        } else if(shot==SHOT_ORBIT) {
            float orbit=angle+(float)frame*.0012f;
            wanted=add(center,v3(sinf(orbit)*115,40,cosf(orbit)*115));
            look=add(center,scale(forward,8));camera.fovy=48;
        } else if(shot==SHOT_DUEL) {
            require(target_index>=0,"duel camera requires --target");
            Vec3 enemy=add(scene.actors[target_index].position,v3(0,8,0));
            Vec3 between=sub(enemy,center);
            float distance=length(between);
            Vec3 side=distance>1?scale(v3(-between.z,0,between.x),1/distance):right;
            look=scale(add(center,enemy),.5f);
            wanted=add(look,add(scale(side,(angle<0?-1:1)*fmaxf(55,distance*.58f)),v3(0,30+distance*.12f,0)));
            center=look;camera.fovy=61;
        } else {
            Vec3 middle=scale(add(world_bounds.min,world_bounds.max),.5f);
            float span=fminf(world_bounds.max.x-world_bounds.min.x,world_bounds.max.z-world_bounds.min.z);
            look=add(subject->position,v3(0,12,0));
            wanted=add(middle,v3(sinf(angle)*span*.34f,span*.18f,cosf(angle)*span*.34f));
            center=look;camera.fovy=63;
        }
        if(!frame || tracking==TRACK_FOLLOW) {
            WorldHit obstacle=world_trace(center,wanted,v3(2,2,2));
            if(obstacle.box>=0)wanted=add(center,scale(sub(wanted,center),fmaxf(0,obstacle.fraction-.01f)));
            float follow=shot==SHOT_SHOULDER?1:1-expf(-9.0f/60);
            if(!frame){camera_position=wanted;camera_target=look;}
            camera_position=add(camera_position,scale(sub(wanted,camera_position),follow));
            camera_target=add(camera_target,scale(sub(look,camera_target),follow));
            obstacle=world_trace(center,camera_position,v3(2,2,2));
            if(obstacle.box>=0)camera_position=add(center,scale(sub(camera_position,center),fmaxf(0,obstacle.fraction-.01f)));
        }
        camera.position=(Vector3){camera_position.x,camera_position.y,camera_position.z};
        camera.target=(Vector3){camera_target.x,camera_target.y,camera_target.z};
        if(lens>0)camera.fovy=lens;
        BeginTextureMode(target);
        Color top={world_background[0][0],world_background[0][1],world_background[0][2],255};
        Color bottom={world_background[1][0],world_background[1][1],world_background[1][2],255};
        ClearBackground(top);
        DrawRectangleGradientV(0,0,target.texture.width,target.texture.height,top,bottom);
        BeginMode3D(camera);rlDisableBackfaceCulling();
        map_visuals_draw();map_visuals_scenery();
        gostek_draw_shadows(&scene,1);
        gostek_begin();
        for(int i=0;i<ACTOR_COUNT;++i)
            gostek_draw(&scene.actors[i],i,game.tick,1,(held[i]&INPUT_JETS)!=0,i==actor_index?body_visibility:1,bones[i]);
        gostek_end();
        effects_draw(&effects,&scene,camera,alpha,timeline+1,held);
        EndMode3D();
        float unit=(float)target.texture.width/1920;
        if(card) {
            require(!strcmp(card,"intro") || !strcmp(card,"outro"),"card must be intro or outro");
            DrawRectangle(0,0,target.texture.width,target.texture.height,(Color){9,14,16,205});
            float size=168*unit,spacing=15*unit;
            Vector2 measure=MeasureTextEx(font,"SOLDAT",size,spacing);
            float x=((float)target.texture.width-measure.x)*.5f;
            float y=(float)target.texture.height*.32f;
            DrawTextEx(font,"SOLDAT",(Vector2){x,y},size,spacing,(Color){238,235,217,255});
            DrawRectangle((int)x,(int)(y+182*unit),(int)measure.x,(int)(3*unit),(Color){193,56,35,255});
            DrawTextEx(font,"3D",(Vector2){x+measure.x-116*unit,y+202*unit},72*unit,5*unit,(Color){193,56,35,255});
            if(!strcmp(card,"outro")) {
                const char *credit="Original assets: OpenSoldat contributors / CC BY 4.0";
                measure=MeasureTextEx(font,credit,19*unit,1*unit);
                DrawTextEx(font,credit,(Vector2){((float)target.texture.width-measure.x)*.5f,(float)target.texture.height-85*unit},19*unit,1*unit,(Color){150,160,156,255});
            }
        }
        EndTextureMode();
        Image pixels=LoadImageFromTexture(target.texture);
        require(pixels.data && pixels.format==PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,"RGBA capture readback failed");
        size_t row=(size_t)pixels.width*4;
        for(int y=pixels.height-1;y>=0;--y)
            require(fwrite((unsigned char *)pixels.data+(size_t)y*row,1,row,video)==row,"encoder frame write failed");
        UnloadImage(pixels);
        if(audio && (held[actor_index]&INPUT_JETS) && subject->life==ALIVE && subject->fuel>0) {
            for(size_t sample=0;sample<800;++sample) {
                size_t source=((size_t)frame*800+sample)%jets.frames;
                audio[((size_t)frame*800+sample)*2]+=jets.samples[source*2]*.2f;
                audio[((size_t)frame*800+sample)*2+1]+=jets.samples[source*2+1]*.2f;
            }
        }
    }
    require(fclose(video)==0,"encoder input close failed");
    int status;
    require(waitpid(encoder,&status,0)==encoder && WIFEXITED(status) && WEXITSTATUS(status)==0,"video encoder failed");
    if(events)require(fclose(events)==0,"event log close failed");
    if(audio) {
        for(size_t i=0;i<sample_count*2;++i)audio[i]=tanhf(audio[i]*.8f);
        Wave wave={.frameCount=(unsigned)sample_count,.sampleRate=48000,.sampleSize=32,.channels=2,.data=audio};
        require(ExportWave(wave,audio_path),"soundtrack export failed");
        free(audio);
        for(int i=0;i<WEAPON_COUNT;++i)if(sounds[i].samples)UnloadWaveSamples(sounds[i].samples);
        UnloadWaveSamples(explosion.samples);UnloadWaveSamples(rocket.samples);UnloadWaveSamples(jets.samples);
    }
    fprintf(stderr,"FILM map=%s preset=%s seed=%u start=%u frames=%u speed=%.3f actor=%d kills=%u end_tick=%" PRIu64 " output=%s\n",map,preset,seed,start,frames,speed,actor_index,kills,game.tick,output);
    if(card)UnloadFont(font);
    UnloadRenderTexture(target);effects_unload(&effects);gostek_free();map_visuals_free();
#ifdef __APPLE__
    rlglClose();CGLSetCurrentContext(NULL);CGLReleaseContext(context);dlclose(framework);
#else
    CloseWindow();
#endif
    game_free(&game);ragdolls_free();poses_free();world_free();
    return 0;
}
