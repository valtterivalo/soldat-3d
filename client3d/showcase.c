#include "showcase.h"
#include "world_layouts.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

const char *const showcase_presets[]={"match","crossfire","rifles","marksmen","ascent"};
const size_t showcase_preset_count=sizeof(showcase_presets)/sizeof(*showcase_presets);

void showcase_init(Game *game,const char *preset,uint32_t seed,int actors)
{
    assert(actors>=2 && actors<=ACTOR_COUNT && seed);
    size_t selected=0;
    while(selected<showcase_preset_count && strcmp(preset,showcase_presets[selected]))++selected;
    assert(selected<showcase_preset_count);
    const WeaponId primary[]={AK74,BARRETT,M249,MP5,SPAS12,STEYRAUG};
    const WeaponId secondary[]={LAW,COLT,COLT,LAW,COLT,LAW};
    game_init(game,seed,selected==0?MODE_DEATHMATCH:MODE_TEAMMATCH);
    for(int i=0;i<ACTOR_COUNT;++i) {
        Actor *actor=&game->actors[i];actor->life=i<actors?ALIVE:INACTIVE;
        actor->team=selected==0?TEAM_NONE:(Team)(TEAM_ALPHA+i%2);
        WeaponId gun=primary[(size_t)i%(sizeof(primary)/sizeof(*primary))];
        WeaponId backup=secondary[(size_t)i%(sizeof(secondary)/sizeof(*secondary))];
        if(selected==1){gun=i%3==0?M249:AK74;backup=LAW;}
        if(selected==2){gun=i%2?M249:AK74;backup=COLT;}
        if(selected==3){gun=BARRETT;backup=COLT;}
        game_equip(actor,gun,backup);
    }
    game_restart(game);game->score_limit=0;game->time_limit_ticks=0;
    if(selected==0)return;
    const Layout *layout=world_layout(world_map_names[world_map_current]);
    const LayoutRoom *room=&layout->rooms[layout->neutral_room];
    Vec3 anchors[2]={v3(room->x-room->width*.34f,room->y+.05f,room->z),
        v3(room->x+room->width*.34f,room->y+.05f,room->z)};
    if(selected==3) {
        float longest=0;
        for(size_t i=0;i<world_nav_node_count;++i)for(size_t j=i+1;j<world_nav_node_count;++j) {
            Vec3 a=world_nav_nodes[i].position,b=world_nav_nodes[j].position;
            float distance=length(sub(a,b));
            if(distance<=longest)continue;
            WorldHit sight=world_trace_for(add(a,v3(0,10,0)),add(b,v3(0,10,0)),v3(0,0,0),
                (WorldQuery){WORLD_TRACE_BULLET,TEAM_NONE,WORLD_NO_FLAG});
            if(sight.box<0){longest=distance;anchors[0]=a;anchors[1]=b;}
        }
        assert(longest>0);
    }
    if(selected==4) {
        float rise=0;
        for(size_t i=0;i<world_nav_link_count;++i) {
            const NavLink *link=&world_nav_links[i];
            if(link->mode!=NAV_JET)continue;
            Vec3 a=world_nav_nodes[link->from].position,b=world_nav_nodes[link->to].position;
            if(b.y-a.y>rise){rise=b.y-a.y;anchors[0]=a;anchors[1]=b;}
        }
        assert(rise>0);
    }
    size_t capacity=world_nav_node_count;
    for(size_t i=0;i<layout->room_count;++i) {
        const LayoutRoom *r=&layout->rooms[i];
        capacity+=(size_t)floorf((r->width-14)/18+1)*(size_t)floorf((r->depth-14)/18+1);
    }
    Vec3 *candidates=malloc(capacity*sizeof(*candidates));assert(candidates);
    size_t count=world_nav_node_count;
    for(size_t i=0;i<count;++i)candidates[i]=world_nav_nodes[i].position;
    for(size_t i=0;i<layout->room_count;++i) {
        const LayoutRoom *r=&layout->rooms[i];
        unsigned nx=(unsigned)floorf((r->width-14)/18+1),nz=(unsigned)floorf((r->depth-14)/18+1);
        for(unsigned x=0;x<nx;++x)for(unsigned z=0;z<nz;++z) {
            Vec3 position=v3(r->x+((float)x-(float)(nx-1)*.5f)*18,r->y+.05f,r->z+((float)z-(float)(nz-1)*.5f)*18);
            assert(count<capacity);candidates[count++]=position;
        }
    }
    for(int i=0;i<actors;++i) {
        Actor *actor=&game->actors[i];Vec3 anchor=anchors[i%2];
        float nearest=INFINITY;size_t choice=count;
        WorldQuery query={WORLD_TRACE_ACTOR,actor->team,WORLD_NO_FLAG};
        for(size_t point=0;point<count;++point) {
            Vec3 position=candidates[point],offset=sub(position,anchor);
            float distance=dot(offset,offset)+offset.y*offset.y*4;
            if(distance>=nearest)continue;
            if(!world_pose_clear_for(position,STANDING,query))continue;
            WorldHit support=world_trace_for(add(position,v3(0,.1f,0)),sub(position,v3(0,1,0)),v3(0,0,0),query);
            if(support.box<0 || support.normal.y<.5f)continue;
            Actor staged=*actor;staged.position=position;unsigned type=world_contact_type(&staged);
            if(type==5 || type==6 || type==7 || type==9 || type==18 || type==20)continue;
            int occupied=0;
            for(int other=0;other<i;++other) {
                Vec3 delta=sub(position,game->actors[other].position);
                if(fabsf(delta.y)<actor_height(STANDING) && delta.x*delta.x+delta.z*delta.z<196){occupied=1;break;}
            }
            if(!occupied){nearest=distance;choice=point;}
        }
        assert(choice<count);
        actor->position=actor->previous=candidates[choice];
        Vec3 toward=sub(anchors[1-i%2],actor->position);
        actor->yaw=atan2f(toward.x,toward.z);
    }
    free(candidates);
}
