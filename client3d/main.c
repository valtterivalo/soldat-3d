#include "game.h"
#include "camera.h"
#include "world.h"
#include "generated_rules.h"
#include "effects.h"
#include "gostek.h"
#include "interface.h"
#include "map_visuals.h"
#include "pose.h"
#include "ragdoll.h"
#include "network.h"
#include "pickups.h"
#include "objectives.h"
#include "lobby.h"
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#define GLFW_INCLUDE_NONE
#include "GLFW/glfw3.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef enum { MENU, PLAYING, BROWSER } Screen;
typedef enum { INTERACTIVE, CAPTURE_MENU, CAPTURE_SOLDIER, CAPTURE_MAP, CAPTURE_PLAN } ViewMode;
typedef enum { JOIN_IDLE, JOIN_SELECTED } JoinState;
typedef enum { OFFLINE, HOST, CLIENT } Session;
typedef enum { PRESENT_IMMEDIATE, PRESENT_VSYNC } FrameSync;
typedef enum { STATS_HIDDEN, STATS_VISIBLE } FrameStats;
typedef struct { double total, simulation, terrain, actors, effects; } FrameSample;
static int frame_compare(const void *a,const void *b) {
    double x=((const FrameSample *)a)->total,y=((const FrameSample *)b)->total;
    return (x>y)-(x<y);
}
typedef struct {
    Sound shots[WEAPON_COUNT], reloads[COLT+1], pickups[PICKUP_BOW+1];
    Sound explosion, rocket_explosion, cluster_explosion, cluster_open, drop, jets, flag, flag_return, capture;
    int muted;
} Audio;
typedef struct {
    Effects effects;
    Audio audio;
    WeaponState weapon;
    uint64_t tick;
    int hitmarker, feed_ticks;
    char killfeed[128];
} Presentation;
static const char *shot_files[WEAPON_COUNT]={
    "deserteagle-fire","mp5-fire","ak74-fire","steyraug-fire","spas12-fire","ruger77-fire",
    "m79-fire","barretm82-fire","m249-fire","minigun-fire","colt1911-fire","slash",
    "chainsaw-r","law","bow-fire","bow-fire","flamer","m2fire",NULL,
    "grenade-throw","grenade-throw",NULL,"throwgun"
};
static const char *pickup_files[PICKUP_BOW+1]={
    "takegun","takemedikit","pickupgun","godflame","predator","vesttake","berserker","pickupgun","takebow"
};
static InputPresses pending_presses;
static GLFWkeyfun raylib_key_callback;
static GLFWmousebuttonfun raylib_mouse_callback;

static void key_callback(GLFWwindow *window,int key,int scancode,int action,int mods) {
    raylib_key_callback(window,key,scancode,action,mods);
    if (action==GLFW_PRESS && key!=GLFW_KEY_UNKNOWN) pending_presses.keys[key]=1;
}

static void mouse_callback(GLFWwindow *window,int button,int action,int mods) {
    raylib_mouse_callback(window,button,action,mods);
    if (action==GLFW_PRESS) pending_presses.mouse|=1u<<button;
}

static Vec3 gv(Vector3 v) { return v3(v.x, v.y, v.z); }

static void screenshot_save(const char *path) {
    rlDrawRenderBatchActive();
    Image capture=LoadImageFromScreen();
    if (!ExportImage(capture,path)) { fprintf(stderr,"Failed to save screenshot: %s\n",path); exit(1); }
    UnloadImage(capture);
}

static Sound sound_load(const char *name) {
    Sound sound=LoadSound(TextFormat("%s/sfx/%s.wav",SOLDAT_ASSET_DIR,name));
    if (!IsSoundValid(sound)) {fprintf(stderr,"Failed to load sound %s\n",name);exit(EXIT_FAILURE);}
    return sound;
}

static void present_state(Presentation *view,const Game *game,int local_actor) {
    effects_step(&view->effects,game,view->tick);
    if (local_actor<0) return;
    const Actor *local=&game->actors[local_actor];
    const WeaponState *gun=&local->slots[local->active_slot];
    Audio *audio=&view->audio;
    if (!audio->muted && gun->id<=COLT &&
        ((gun->id!=SPAS12 && view->weapon.phase==WEAPON_READY && gun->phase==WEAPON_RELOADING) ||
        (gun->id==SPAS12 && gun->phase==WEAPON_RELOADING && gun->reload_count!=view->weapon.reload_count &&
            gun->reload_count==(SRC_RELOAD_FRAMES-2)*2-12)))
        PlaySound(audio->reloads[gun->id]);
    view->weapon=*gun;
    for (size_t e=0;e<game->event_count;++e) {
        GameEvent event=game->events[e];
        if (event.kind==EVENT_SHOT) gostek_fire(event.actor,view->tick);
        if (event.kind==EVENT_HIT && event.actor==local_actor) view->hitmarker=9;
        if (event.kind==EVENT_KILL) {
            snprintf(view->killfeed,sizeof(view->killfeed),"%s  %s  %s",game->names[event.actor],weapons[event.weapon].name,game->names[event.target]);
            view->feed_ticks=240;
        }
        if (event.kind>=EVENT_FLAG_GRAB) {
            const char *action=event.kind==EVENT_FLAG_GRAB ? "took" : event.kind==EVENT_FLAG_RETURN ? "returned" :
                event.kind==EVENT_FLAG_DROP ? "dropped" : "captured";
            const char *flag=event.target==FLAG_ALPHA ? "Alpha flag" : event.target==FLAG_BRAVO ? "Bravo flag" : "yellow flag";
            snprintf(view->killfeed,sizeof(view->killfeed),"%s %s %s",event.actor<0 ? "Server" : game->names[event.actor],action,flag);
            view->feed_ticks=240;
        }
        if (audio->muted) continue;
        Sound sound;
        if (event.kind==EVENT_SHOT) {
            if (!shot_files[event.weapon] || game->actors[event.actor].bonus==BONUS_PREDATOR) continue;
            sound=audio->shots[event.weapon];
        } else if (event.kind==EVENT_EXPLOSION) {
            sound=event.weapon==FRAGGRENADE ? audio->explosion :
                event.weapon==CLUSTERGRENADE ? audio->cluster_open :
                event.weapon==CLUSTER ? audio->cluster_explosion : audio->rocket_explosion;
        } else if (event.kind==EVENT_PICKUP) sound=audio->pickups[event.target];
        else if (event.kind==EVENT_DROP) sound=audio->drop;
        else if (event.kind==EVENT_FLAG_GRAB || event.kind==EVENT_FLAG_DROP) sound=audio->flag;
        else if (event.kind==EVENT_FLAG_RETURN) sound=audio->flag_return;
        else if (event.kind==EVENT_FLAG_CAPTURE) sound=audio->capture;
        else continue;
        float distance=length(sub(event.position,local->position));
        SetSoundVolume(sound,fmaxf(0,1-distance/750));
        if ((event.weapon!=CHAINSAW && event.weapon!=FLAMER) || !IsSoundPlaying(sound)) PlaySound(sound);
    }
}

static Camera3D render_camera(ShoulderView view) {
    return (Camera3D){.position={view.position.x,view.position.y,view.position.z},
        .target={view.target.x,view.target.y,view.target.z},.up={0,1,0},.fovy=view.fov,
        .projection=CAMERA_PERSPECTIVE};
}

static int map_capture(ViewMode mode) {
    return mode==CAPTURE_MAP || mode==CAPTURE_PLAN;
}

int main(int argc,char **argv) {
    int demo_frames=0,benchmark_frames=0,muted=0,bots=6,bonuses=0;
    FrameSync sync=PRESENT_IMMEDIATE;
    FrameStats stats=STATS_HIDDEN;
    int score_limit=-1,time_limit=-1,friendly_fire=0,browse=0,quick_join=0;
    unsigned short port=23073;
    unsigned short lobby_port=LOBBY_PORT;
    Session session=OFFLINE;
    const char *host=NULL,*name="Soldier",*map="Arena2";
    const char *directory=NULL,*server_name="Soldat 3D";
    GameMode mode=MODE_DEATHMATCH;
    Team chosen_team=TEAM_NONE;
    ViewMode view_mode=INTERACTIVE;
    float view_angle=0;
    const char *screenshot=NULL;
    for (int i=1;i<argc;++i) {
        if (!strcmp(argv[i],"--host") && session==OFFLINE) session=HOST;
        else if (!strcmp(argv[i],"--join") && i+1<argc && session==OFFLINE) {session=CLIENT;host=argv[++i];}
        else if (!strcmp(argv[i],"--name") && i+1<argc) name=argv[++i];
        else if (!strcmp(argv[i],"--map") && i+1<argc) map=argv[++i];
        else if (!strcmp(argv[i],"--lobby") && i+1<argc) directory=argv[++i];
        else if (!strcmp(argv[i],"--server-name") && i+1<argc) server_name=argv[++i];
        else if (!strcmp(argv[i],"--browse")) browse=1;
        else if (!strcmp(argv[i],"--quickjoin")) {browse=1;quick_join=1;}
        else if (!strcmp(argv[i],"--friendly-fire")) friendly_fire=1;
        else if (!strcmp(argv[i],"--team") && i+1<argc) {
            const char *value=argv[++i];
            int found=!strcmp(value,"auto");
            for (int team=TEAM_NONE;team<=TEAM_SPECTATOR;++team)
                if (!strcasecmp(value,game_team_names[team])) {chosen_team=(Team)team;found=1;}
            if (!found) {fprintf(stderr,"Unknown team %s\n",value);return 2;}
        }
        else if (!strcmp(argv[i],"--list-maps")) {
            for (size_t n=0;n<world_map_count;++n) puts(world_map_names[n]);
            return 0;
        } else if (!strcmp(argv[i],"--mode") && i+1<argc) {
            const char *value=argv[++i];
            int found=0;
            for (int m=MODE_DEATHMATCH;m<=MODE_HTF;++m)
                if (!strcmp(value,game_mode_ids[m])) {mode=(GameMode)m;found=1;}
            if (!found) {fprintf(stderr,"Unknown mode %s\n",value);return 2;}
        } else if (i+1<argc && (!strcmp(argv[i],"--port") || !strcmp(argv[i],"--bots") ||
            !strcmp(argv[i],"--bonuses") || !strcmp(argv[i],"--demo-frames") || !strcmp(argv[i],"--benchmark") || !strcmp(argv[i],"--lobby-port") ||
            !strcmp(argv[i],"--score-limit") || !strcmp(argv[i],"--time-limit"))) {
            const char *option=argv[i++];
            char *end;
            errno=0;
            unsigned long value=strtoul(argv[i],&end,10);
            if (errno || end==argv[i] || *end || argv[i][0]=='-') {fprintf(stderr,"Invalid %s\n",option);return 2;}
            if (!strcmp(option,"--port") && value>0 && value<=65535) port=(unsigned short)value;
            else if (!strcmp(option,"--lobby-port") && value>0 && value<=65535) lobby_port=(unsigned short)value;
            else if (!strcmp(option,"--bots") && value<ACTOR_COUNT) bots=(int)value;
            else if (!strcmp(option,"--bonuses") && value<=5) bonuses=(int)value;
            else if (!strcmp(option,"--demo-frames") && value>0 && value<=INT_MAX) demo_frames=(int)value;
            else if (!strcmp(option,"--benchmark") && value>0 && value<=INT_MAX) demo_frames=benchmark_frames=(int)value;
            else if (!strcmp(option,"--score-limit") && value<=INT_MAX) score_limit=(int)value;
            else if (!strcmp(option,"--time-limit") && value<=INT_MAX/(60*TICK_RATE)) time_limit=(int)value;
            else {fprintf(stderr,"Invalid %s\n",option);return 2;}
        } else if (!strcmp(argv[i],"--screenshot") && i+1<argc) screenshot=argv[++i];
        else if (!strcmp(argv[i],"--mute")) muted=1;
        else if (!strcmp(argv[i],"--vsync")) sync=PRESENT_VSYNC;
        else if (!strcmp(argv[i],"--menu-frame")) view_mode=CAPTURE_MENU;
        else if (!strcmp(argv[i],"--map-plan")) view_mode=CAPTURE_PLAN;
        else if ((!strcmp(argv[i],"--asset-view") || !strcmp(argv[i],"--map-view")) && i+1<argc) {
            view_mode=!strcmp(argv[i],"--asset-view") ? CAPTURE_SOLDIER : CAPTURE_MAP;
            char *end;
            view_angle=strtof(argv[++i],&end)*DEG2RAD;
            if (end==argv[i] || *end) {fprintf(stderr,"Invalid view angle\n");return 2;}
        } else {
            fprintf(stderr,"Usage: %s [--host | --join hostname | --browse | --quickjoin] [--lobby hostname] [--lobby-port 23074] [--server-name name] [--port 23073] [--name name] [--team auto|alpha|bravo|charlie|delta|spectator] [--map Arena2] [--list-maps] [--bots 0..31] [--bonuses 0..5] [--mode deathmatch|pointmatch|teammatch|ctf|rambo|inf|htf] [--score-limit N] [--time-limit minutes] [--friendly-fire] [--demo-frames N] [--benchmark N] [--vsync] [--screenshot path.png] [--menu-frame] [--asset-view degrees] [--map-view degrees | --map-plan] [--mute]\n",argv[0]);
            return 2;
        }
    }
    if (!*name || strlen(name)>PLAYER_NAME_LENGTH) {fprintf(stderr,"Player names require 1..%d characters\n",PLAYER_NAME_LENGTH);return 2;}
    size_t map_index=world_map_index(map);
    if (map_index==SIZE_MAX) {fprintf(stderr,"Unknown map %s\n",map);return 2;}
    world_load(map_index);
    if (!world_supports_mode(mode)) {
        fprintf(stderr,"Map %s does not support %s. Choose a map with the required team and objective spawns.\n",map,game_mode_ids[mode]);
        return 2;
    }
    if (chosen_team>=TEAM_ALPHA && chosen_team<=TEAM_DELTA && (!game_team_mode(mode) ||
        (mode!=MODE_TEAMMATCH && chosen_team>TEAM_BRAVO) || world_team_spawn_count(chosen_team)==0) && session!=CLIENT) {
        fprintf(stderr,"Team %s is unavailable on %s in %s\n",game_team_names[chosen_team],map,game_mode_ids[mode]);return 2;
    }
    poses_init();
    ragdolls_init();
    Game game;
    game_init(&game,0x501da7,mode);
    game.bonus_frequency=bonuses;
    strcpy(game.names[0],name);
    for (int i=0;i<ACTOR_COUNT;++i) {
        if (session==CLIENT || i>bots) game.actors[i].life=INACTIVE;
        else game.actors[i].life=ALIVE;
    }
    game_set_mode(&game,mode);
    game.friendly_fire=friendly_fire;
    if (score_limit>=0) game.score_limit=score_limit;
    if (time_limit>=0) game.time_limit_ticks=time_limit*60*TICK_RATE;
    if (session!=CLIENT) game_select_team(&game,0,chosen_team);
    const char *names[ACTOR_COUNT];
    for (int i=0;i<ACTOR_COUNT;++i) names[i]=game.names[i];
    Network *network=session==HOST ? network_host(port,0,&game) :
        session==CLIENT ? network_join(host,port,name) : NULL;
    LobbyHost *advertisement=NULL;
    if (session==HOST) {
        network_set_name(network,server_name);
        advertisement=lobby_host_open(network_socket(network),directory,lobby_port,server_name);
    } else if (session==CLIENT) network_select_team(network,chosen_team);
    Lobby *lobby=lobby_open(directory,lobby_port);
    int selected_server=0;
    if (browse) lobby_refresh(lobby,network_time());
    int local_actor=session==CLIENT ? -1 : 0;
    if (session==HOST) printf("Hosting UDP %u on %s\n",network_port(network),world_map_names[world_map_current]);
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE | (sync==PRESENT_VSYNC ? FLAG_VSYNC_HINT : 0));
    InitWindow(1280,800,TextFormat("Soldat 3D | %s",world_map_names[world_map_current]));
    _Static_assert(KEY_KB_MENU==GLFW_KEY_LAST,"Keyboard codes match the GLFW backend");
    raylib_key_callback=glfwSetKeyCallback(glfwGetCurrentContext(),key_callback);
    raylib_mouse_callback=glfwSetMouseButtonCallback(glfwGetCurrentContext(),mouse_callback);
    SetWindowMinSize(1024,700);
    SetExitKey(KEY_NULL);
    SetTargetFPS(0);
    rlSetClipPlanes(.2,2*length(sub(world_bounds.max,world_bounds.min))+1000);
    map_visuals_init();
    gostek_init();
    Interface interface=interface_load();
    Presentation view={.effects=effects_load(),.audio={.muted=muted}};
    Audio *audio=&view.audio;
    if (!muted) {
        InitAudioDevice();
        if (!IsAudioDeviceReady()) {fprintf(stderr,"Audio device initialization failed\n");return 1;}
        SetMasterVolume(.48f);
        for (int i=0;i<WEAPON_COUNT;++i) if (shot_files[i]) audio->shots[i]=sound_load(shot_files[i]);
        const char *reload_files[COLT+1]={"deserteagle-reload","mp5-reload","ak74-reload","steyraug-reload","spas12-reload","ruger77-reload","m79-reload","barretm82-reload","m249-reload","minigun-reload","colt1911-reload"};
        for (int i=0;i<=COLT;++i) audio->reloads[i]=sound_load(reload_files[i]);
        for (int i=0;i<=PICKUP_BOW;++i) audio->pickups[i]=sound_load(pickup_files[i]);
        audio->explosion=sound_load("grenade-explosion");
        audio->rocket_explosion=sound_load("m79-explosion");
        audio->cluster_explosion=sound_load("cluster-explosion");
        audio->cluster_open=sound_load("clustergrenade");
        audio->drop=sound_load("throwgun");
        audio->jets=sound_load("rocketz");
        audio->flag=sound_load("flag");audio->flag_return=sound_load("flag2");audio->capture=sound_load("capture");
    }
    Screen screen=browse ? BROWSER : demo_frames>0 ? PLAYING : MENU;
    WeaponId selected=AK74,secondary=COLT;
    float yaw=1.5707963f,pitch=0,shoulder=1,fov=72;
    int frames=0;
    int intermission_ticks=0;
    JoinState join_server=JOIN_IDLE;
    LobbyEntry selected_entry={0};
    double accumulator=0;
    uint32_t last_held[ACTOR_COUNT]={0},pending=0;
    Game aim_scene=game;
    Vec3 aim_poses[ACTOR_COUNT][21]={0};
    for(int i=0;i<ACTOR_COUNT;++i)if(game.actors[i].life==ALIVE)actor_pose(&game.actors[i],aim_poses[i]);
    Actor observer=game.actors[0];
    observer.life=ALIVE;
    CameraRig camera_rig={48};
    ShoulderView shoulder_view=camera_view(&camera_rig,&observer,yaw,pitch,1,shoulder,72,1);
    Camera3D camera=render_camera(shoulder_view);
    FrameSample *samples=benchmark_frames ? calloc((size_t)benchmark_frames,sizeof(*samples)) : NULL;
    if(benchmark_frames && !samples)abort();
    HideCursor();
    while (!WindowShouldClose()) {
        double frame_start=GetTime();
        FrameSample sample={0};
        InputPresses presses=pending_presses;
        pending_presses=(InputPresses){0};
        double frame_time=GetFrameTime();
        if(screen==PLAYING && presses.keys[KEY_F3])stats=stats==STATS_HIDDEN ? STATS_VISIBLE : STATS_HIDDEN;
        if (presses.keys[KEY_ESCAPE]) {
            quick_join=0;
            screen=screen==BROWSER ? MENU : screen==PLAYING ? MENU : PLAYING;
            if (screen==PLAYING) DisableCursor(); else { EnableCursor(); HideCursor(); }
            accumulator=0;
            pending=0;
        }
        if (screen!=PLAYING && (presses.keys[KEY_F5] || presses.keys[KEY_B])) {
            screen=BROWSER;selected_server=0;
            lobby_refresh(lobby,network_time());
        }
        lobby_pump(lobby,network_time());
        if (advertisement) lobby_host_update(advertisement,network_time());
        if (screen==BROWSER && (presses.keys[KEY_F6] || quick_join)) {
            int best=lobby_best(lobby);
            if (best>=0) {selected_entry=*lobby_entry(lobby,(size_t)best);join_server=JOIN_SELECTED;quick_join=0;}
        }
        if (join_server==JOIN_SELECTED) {
            if (advertisement) {lobby_host_close(advertisement);advertisement=NULL;}
            if (network) network_close(network);
            for (int i=0;i<ACTOR_COUNT;++i) game.actors[i].life=INACTIVE;
            network=network_join(selected_entry.address,selected_entry.port,name);
            network_select_team(network,chosen_team);
            session=CLIENT;local_actor=-1;screen=demo_frames ? PLAYING:MENU;
            view.effects.count=0;view.hitmarker=view.feed_ticks=0;aim_scene=game;
            accumulator=0;pending=0;join_server=JOIN_IDLE;
            memset(last_held,0,sizeof(last_held));
        }
        if (screen==MENU && presses.keys[KEY_H] && session==OFFLINE) {
            network=network_host(port,0,&game);
            network_set_name(network,server_name);
            advertisement=lobby_host_open(network_socket(network),directory,lobby_port,server_name);
            session=HOST;
        }
        if (screen==MENU && presses.keys[KEY_F10] && network) {
            if (advertisement) {lobby_host_close(advertisement);advertisement=NULL;}
            network_close(network);network=NULL;session=OFFLINE;local_actor=0;
            for (int i=0;i<ACTOR_COUNT;++i) {
                game.actors[i].life=i<=bots ? ALIVE:INACTIVE;
                game.actors[i].team=TEAM_NONE;
            }
            strcpy(game.names[0],name);
            game_restart(&game);game_select_team(&game,0,chosen_team);
            view.effects.count=0;intermission_ticks=0;aim_scene=game;
        }
        if (screen==MENU && presses.keys[KEY_T]) {
            if (!game_team_mode(game.mode)) chosen_team=chosen_team==TEAM_SPECTATOR ? TEAM_NONE:TEAM_SPECTATOR;
            else {
                do {
                    chosen_team=(Team)((chosen_team+1)%(TEAM_SPECTATOR+1));
                    if (game.mode!=MODE_TEAMMATCH && chosen_team==TEAM_CHARLIE) chosen_team=TEAM_SPECTATOR;
                } while (chosen_team>=TEAM_ALPHA && chosen_team<=TEAM_DELTA && world_team_spawn_count(chosen_team)==0);
            }
            if (session==CLIENT) network_select_team(network,chosen_team);
            else game_select_team(&game,local_actor,chosen_team);
        }
        if (network) {
            int updated=network_receive(network,&game);
            local_actor=network_actor(network);
            if (session==CLIENT && network_map(network)!=world_map_current) {
                map_visuals_free();world_load(network_map(network));map_visuals_init();
                rlSetClipPlanes(.2,2*length(sub(world_bounds.max,world_bounds.min))+1000);
                view.effects.count=0;aim_scene=game;
                SetWindowTitle(TextFormat("Soldat 3D | %s",world_map_names[world_map_current]));
            }
            if (session==CLIENT && updated) {
                if (chosen_team>=TEAM_ALPHA && chosen_team<=TEAM_DELTA && (!game_team_mode(game.mode) ||
                    (game.mode!=MODE_TEAMMATCH && chosen_team>TEAM_BRAVO) || world_team_spawn_count(chosen_team)==0)) {
                    chosen_team=TEAM_NONE;network_select_team(network,chosen_team);
                }
                for (int i=0;i<ACTOR_COUNT;++i) if (i!=local_actor) last_held[i]=game.actors[i].controls;
            }
        }
        if (screen==MENU && session!=CLIENT &&
            (presses.keys[KEY_M] || presses.keys[KEY_PAGE_UP] || presses.keys[KEY_PAGE_DOWN])) {
            GameMode next_mode=presses.keys[KEY_M] ? (GameMode)((game.mode+1)%(MODE_HTF+1)):game.mode;
            if (presses.keys[KEY_PAGE_UP] || presses.keys[KEY_PAGE_DOWN] || !world_supports_mode(next_mode)) {
                map_visuals_free();
                do {
                    size_t next=(world_map_current+world_map_count+(presses.keys[KEY_PAGE_UP]?-1:1))%world_map_count;
                    world_load(next);
                } while (!world_supports_mode(next_mode));
                map_visuals_init();
                rlSetClipPlanes(.2,2*length(sub(world_bounds.max,world_bounds.min))+1000);
                SetWindowTitle(TextFormat("Soldat 3D | %s",world_map_names[world_map_current]));
            }
            if (presses.keys[KEY_M]) {
                game_set_mode(&game,next_mode);
                if (chosen_team!=TEAM_SPECTATOR) chosen_team=TEAM_NONE;
            } else game_restart(&game);
            intermission_ticks=0;
            view.effects.count=0;view.hitmarker=view.feed_ticks=0;aim_scene=game;
            memset(last_held,0,sizeof(last_held));pending=0;
        }
        float target_fov=local_actor>=0 ? camera_focus_fov(&game.actors[local_actor],
            screen==PLAYING && IsKeyDown(KEY_LEFT_ALT) ? FOCUS_PRECISION : FOCUS_NONE) : 72;
        fov=target_fov+(fov-target_fov)*expf(-20*(float)frame_time);
        if (screen==PLAYING && !demo_frames) {
            Vector2 mouse=GetMouseDelta();
            float sensitivity=.0025f*tanf(fov*DEG2RAD*.5f)/tanf(72*DEG2RAD*.5f);
            yaw-=mouse.x*sensitivity;
            pitch=Clamp(pitch-mouse.y*sensitivity,-1.35f,1.35f);
            if (presses.keys[KEY_V]) shoulder=-shoulder;
        }
        uint32_t held=0;
        if (screen==PLAYING) {
            if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) held|=INPUT_FIRE;
            if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) held|=INPUT_JETS;
            const int keys[]={KEY_SPACE,KEY_LEFT_CONTROL,KEY_X,KEY_R,KEY_E,KEY_Q,KEY_LEFT_SHIFT,KEY_F,KEY_G};
            const uint32_t actions[]={INPUT_JUMP,INPUT_CROUCH,INPUT_PRONE,INPUT_RELOAD,INPUT_GRENADE,INPUT_SWITCH,INPUT_ROLL,INPUT_THROW,INPUT_FLAG_THROW};
            for (size_t i=0;i<sizeof(keys)/sizeof(keys[0]);++i) {
                if (IsKeyDown(keys[i])) held|=actions[i];
                if (presses.keys[keys[i]]) pending|=actions[i];
            }
            if (presses.mouse&(1u<<MOUSE_BUTTON_LEFT)) pending|=INPUT_FIRE;
            if (presses.mouse&(1u<<MOUSE_BUTTON_RIGHT)) pending|=INPUT_JETS;
            if (local_actor>=0) pending|=held&~last_held[local_actor];
        }
        if (screen==PLAYING || network) {
            accumulator+=demo_frames && session==OFFLINE ? 1.0/TICK_RATE : frame_time;
            while (accumulator>=1.0/TICK_RATE) {
                ++view.tick;
                if(view.hitmarker>0)--view.hitmarker;
                if(view.feed_ticks>0)--view.feed_ticks;
                Input inputs[ACTOR_COUNT]={0};
                if (session!=CLIENT) {
                    if (network) network_receive(network,&game);
                    for (int i=0;i<ACTOR_COUNT;++i) {
                        if (i==local_actor || (network && network_remote(network,i))) continue;
                        inputs[i]=bot_input(&game,i);
                        inputs[i].pressed=inputs[i].held&~last_held[i];
                    }
                }
                if (local_actor>=0) {
                    Input input={.yaw=yaw,.pitch=pitch};
                    if (screen==PLAYING && demo_frames) {
                        input=bot_input(&game,local_actor);
                        input.pressed=input.held&~last_held[local_actor];
                        yaw=input.yaw;pitch=input.pitch;
                    } else if (screen==PLAYING) {
                        CameraRig aim_rig=camera_rig;
                        camera=render_camera(camera_view(&aim_rig,&aim_scene.actors[local_actor],yaw,pitch,1,shoulder,fov,0));
                        Vec3 origin=gv(camera.position),far=add(origin,scale(direction(yaw,pitch),1800));
                        Vec3 target=combat_aim_target(&aim_scene,local_actor,origin,far,(const Vec3 (*)[21])aim_poses);
                        Vec3 aim=actor_aim_direction(&game.actors[local_actor],target);
                        float aim_yaw=atan2f(aim.x,aim.z),turn=yaw-aim_yaw;
                        float right=(float)IsKeyDown(KEY_D)-(float)IsKeyDown(KEY_A);
                        float forward=(float)IsKeyDown(KEY_W)-(float)IsKeyDown(KEY_S);
                        input=(Input){.right=right*cosf(turn)-forward*sinf(turn),
                            .forward=forward*cosf(turn)+right*sinf(turn),.yaw=aim_yaw,
                            .pitch=atan2f(aim.y,sqrtf(aim.x*aim.x+aim.z*aim.z)),.held=held|pending,.pressed=pending};
                    }
                    inputs[local_actor]=input;
                    if (session==CLIENT) {
                        network_send_input(network,&game,input,selected,secondary);
                        present_state(&view,&game,local_actor);
                        last_held[local_actor]=input.held;
                    }
                }
                pending=0;
                if (session!=CLIENT) {
                    if (network) network_inputs(network,inputs);
                    for (int i=0;i<ACTOR_COUNT;++i) last_held[i]=inputs[i].held;
                    if (game.phase==MATCH_PLAYING) game_step(&game,inputs);
                    else {
                        game.event_count=0;++game.tick;
                        if (++intermission_ticks>=SRC_DEFAULT_MAPCHANGE_TIME) {
                            game_restart(&game);intermission_ticks=0;
                        }
                    }
                    if (network) network_broadcast(network,&game);
                    present_state(&view,&game,local_actor);
                }
                accumulator-=1.0/TICK_RATE;
            }
        }
        if (!muted) {
            if ((screen==PLAYING || network) && local_actor>=0 && game.actors[local_actor].life==ALIVE &&
                (last_held[local_actor]&INPUT_JETS) && game.actors[local_actor].fuel>0) {
                if (!IsSoundPlaying(audio->jets)) PlaySound(audio->jets);
            } else StopSound(audio->jets);
            int chainsaw_active=0;
            for (int i=0;i<ACTOR_COUNT;++i)
                chainsaw_active|=(screen==PLAYING || network) && game.actors[i].life==ALIVE &&
                    game.actors[i].slots[game.actors[i].active_slot].id==CHAINSAW && (last_held[i]&INPUT_FIRE);
            if (!chainsaw_active) StopSound(audio->shots[CHAINSAW]);
        }
        float alpha=(demo_frames && session==OFFLINE) || (!network && screen==MENU) ? 1 : (float)(accumulator*TICK_RATE);
        const Game *scene=session==CLIENT ? network_render(network,&game,network_time(),alpha) : &game;
        if(session==CLIENT)present_state(&view,scene,local_actor);
        float scene_alpha=session==CLIENT ? 1 : alpha;
        const Actor *player=local_actor>=0 ? &scene->actors[local_actor] : &observer;
        shoulder_view=camera_view(&camera_rig,player,yaw,pitch,scene_alpha,shoulder,fov,(float)frame_time);
        aim_scene=*scene;
        for(int i=0;i<ACTOR_COUNT;++i) {
            Actor *actor=&aim_scene.actors[i];
            actor->position=add(actor->previous,scale(sub(actor->position,actor->previous),scene_alpha));
            actor->previous=actor->position;
            if(actor->life==ALIVE) {
                const Vec3 *points=session==CLIENT ? network_actor_pose(network,i) : NULL;
                if(points)memcpy(aim_poses[i],points,sizeof(aim_poses[i]));
                else actor_pose(actor,aim_poses[i]);
            }
        }
        camera=render_camera(shoulder_view);
        sample.simulation=GetTime()-frame_start;
        BeginDrawing();
        ClearBackground(BLACK);
        map_visuals_background();
        if (view_mode==CAPTURE_SOLDIER) {
            camera=(Camera3D){.position={sinf(view_angle)*48,20,cosf(view_angle)*48},
                .target={0,11,0},.up={0,1,0},.fovy=36,.projection=CAMERA_PERSPECTIVE};
            Actor preview=observer;
            preview.position=preview.previous=v3(0,0,0);
            preview.yaw=preview.pitch=0;
            preview.contact=GROUNDED;
            preview.spawn_protection_ticks=-1;
            preview.animation=MOVE_IDLE;
            BeginMode3D(camera);
            DrawPlane((Vector3){0,-.2f,0},(Vector2){100,100},(Color){73,85,51,255});
            gostek_begin();
            gostek_draw(&preview,0,view.tick,1,0,1,NULL);
            gostek_end();
            EndMode3D();
        } else {
            if (map_capture(view_mode)) {
                Vec3 low=v3(INFINITY,INFINITY,INFINITY),high=v3(-INFINITY,-INFINITY,-INFINITY);
                for(size_t i=0;i<world_solid_count;++i) {
                    const WorldSolid *solid=&world_solids[i];
                    if(solid->texture!=WORLD_TERRAIN)continue;
                    for(unsigned face=0;face<solid->face_count;++face)if(solid->visible_faces&(1u<<face))
                        for(unsigned v=0;v<solid->face_size[face];++v) {
                            Vec3 point=solid->vertices[solid->faces[face][v]];
                            low=v3(fminf(low.x,point.x),fminf(low.y,point.y),fminf(low.z,point.z));
                            high=v3(fmaxf(high.x,point.x),fmaxf(high.y,point.y),fmaxf(high.z,point.z));
                        }
                }
                Vec3 center=scale(add(low,high),.5f);
                Vec3 size=sub(high,low);
                float extent=length(size);
                camera=(Camera3D){.position={center.x+sinf(view_angle)*extent*.65f,
                    center.y+extent*.5f,center.z+cosf(view_angle)*extent*.65f},
                    .target={center.x,center.y,center.z},.up={0,1,0},.fovy=48,.projection=CAMERA_PERSPECTIVE};
                if(view_mode==CAPTURE_PLAN)camera=(Camera3D){
                    .position={center.x,high.y+extent,center.z},
                    .target={center.x,center.y,center.z},.up={0,0,-1},
                    .fovy=1.08f*fmaxf(size.z,size.x*(float)GetScreenHeight()/(float)GetScreenWidth()),
                    .projection=CAMERA_ORTHOGRAPHIC};
            }
            BeginMode3D(camera);
            rlDisableBackfaceCulling();
            double section_start=GetTime();
            map_visuals_draw();
            map_visuals_scenery();
            sample.terrain=GetTime()-section_start;
            section_start=GetTime();
            gostek_draw_shadows(scene,scene_alpha);
            gostek_begin();
            for (int i=0;i<ACTOR_COUNT;++i) {
                float visibility=i==local_actor && !map_capture(view_mode) ? shoulder_view.body_visibility : 1;
                gostek_draw(&scene->actors[i],i,view.tick,scene_alpha,(last_held[i]&INPUT_JETS)!=0,visibility,
                    session==CLIENT ? network_actor_pose(network,i) : NULL);
            }
            gostek_end();
            sample.actors=GetTime()-section_start;
            section_start=GetTime();
            effects_draw(&view.effects,scene,camera,scene_alpha,(double)view.tick+alpha,last_held);
            sample.effects=GetTime()-section_start;
            EndMode3D();
            if (screen==BROWSER) {
                int choice=interface_browser(&interface,lobby,&selected_server,&presses);
                if (choice>=0) {selected_entry=*lobby_entry(lobby,(size_t)choice);join_server=JOIN_SELECTED;}
            }
            else if (!map_capture(view_mode) && screen==PLAYING && local_actor>=0)
                interface_hud(&interface,&game,view.hitmarker,view.killfeed,view.feed_ticks,names,local_actor,camera);
            else if (!map_capture(view_mode) && interface_menu(&interface,&selected,&secondary,&presses)==INTERFACE_PLAY) {
                if (session!=CLIENT && local_actor>=0)
                    game_select_loadout(&game.actors[local_actor],selected,secondary);
                screen=PLAYING;
                accumulator=0;
                DisableCursor();
            }
        }
        if (screen==MENU && view_mode!=CAPTURE_SOLDIER && !map_capture(view_mode)) {
            DrawText(TextFormat("%s%s   %s%s   Team: %s [T]",world_map_names[world_map_current],session==CLIENT?"":" [PgUp/PgDn]",
                game_mode_names[game.mode],session==CLIENT?"":" [M]",game_team_names[chosen_team]),24,GetScreenHeight()-52,16,RAYWHITE);
            DrawText(TextFormat("[B] Servers   %s   %s",session==OFFLINE?"[H] Host":"[F10] Disconnect",network?network_message(network):"Offline"),24,GetScreenHeight()-28,16,RAYWHITE);
        } else if (network && network_status(network)!=NET_CONNECTED)
            DrawText(network_message(network),24,24,20,RAYWHITE);
        if(screen==PLAYING && fov<71.9f)DrawText(TextFormat("%.1fx",tanf(72*DEG2RAD*.5f)/tanf(fov*DEG2RAD*.5f)),GetScreenWidth()/2+24,GetScreenHeight()/2+20,16,RAYWHITE);
        if(stats==STATS_VISIBLE)DrawText(TextFormat("%d FPS   %.2f ms",GetFPS(),GetFrameTime()*1000),24,52,18,RAYWHITE);
        if (presses.keys[KEY_F12]) screenshot_save(screenshot ? screenshot : "soldat3d.png");
        int finished=view_mode!=INTERACTIVE || (demo_frames>0 && frames+1>=demo_frames);
        if (finished && screenshot) screenshot_save(screenshot);
        EndDrawing();
        sample.total=GetTime()-frame_start;
        if(benchmark_frames)samples[frames]=sample;
        ++frames;
        if (finished) {
            printf("demo ticks=%llu projectiles=%zu actor=%d player=(%.2f,%.2f,%.2f) kills=%d deaths=%d spawn=%u map=%s%s\n",
                (unsigned long long)game.tick,game.projectile_count,local_actor,player->position.x,
                player->position.y,player->position.z,player->kills,player->deaths,player->spawn_id,world_map_names[world_map_current],
                network?network_status(network)==NET_CONNECTED?" connected":" disconnected":" offline");
            break;
        }
    }
    if(benchmark_frames) {
        FrameSample sum={0};
        for(int i=0;i<frames;++i) {
            sum.total+=samples[i].total;sum.simulation+=samples[i].simulation;
            sum.terrain+=samples[i].terrain;sum.actors+=samples[i].actors;sum.effects+=samples[i].effects;
        }
        qsort(samples,(size_t)frames,sizeof(*samples),frame_compare);
        printf("RENDER_BENCH map=%s actors=%d frames=%d ticks_per_frame=1 resolution=%dx%d fps=%.1f mean_ms=%.3f median_ms=%.3f p99_ms=%.3f max_ms=%.3f sim_ms=%.3f terrain_ms=%.3f actors_ms=%.3f effects_ms=%.3f\n",
            world_map_names[world_map_current],bots+1,frames,GetRenderWidth(),GetRenderHeight(),
            frames/sum.total,sum.total*1000/frames,samples[frames/2].total*1000,
            samples[(frames-1)*99/100].total*1000,samples[frames-1].total*1000,
            sum.simulation*1000/frames,sum.terrain*1000/frames,sum.actors*1000/frames,sum.effects*1000/frames);
        free(samples);
    }
    if (!muted) {
        for (int i=0;i<WEAPON_COUNT;++i) if (shot_files[i]) UnloadSound(audio->shots[i]);
        for (int i=0;i<=COLT;++i) UnloadSound(audio->reloads[i]);
        for (int i=0;i<=PICKUP_BOW;++i) UnloadSound(audio->pickups[i]);
        UnloadSound(audio->explosion);UnloadSound(audio->rocket_explosion);
        UnloadSound(audio->cluster_explosion);UnloadSound(audio->cluster_open);
        UnloadSound(audio->drop);UnloadSound(audio->jets);
        UnloadSound(audio->flag);UnloadSound(audio->flag_return);UnloadSound(audio->capture);
        CloseAudioDevice();
    }
    if (advertisement) lobby_host_close(advertisement);
    if (network) network_close(network);
    lobby_close(lobby);
    effects_unload(&view.effects);
    interface_unload(&interface);
    gostek_free();
    map_visuals_free();
    CloseWindow();
    game_free(&game);
    ragdolls_free();
    poses_free();
    world_free();
    return 0;
}
