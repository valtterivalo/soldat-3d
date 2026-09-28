#include "ragdoll.h"
#include "pose.h"
#include "world.h"
#include "generated_rules.h"

#include <stdio.h>
#include <stdlib.h>

static void check(int condition,const char *message) {
    if (condition) return;
    fprintf(stderr,"ragdoll: %s\n",message);
    exit(EXIT_FAILURE);
}

static Actor corpse(Vec3 position,Vec3 velocity) {
    Actor actor={.position=position,.previous=sub(position,velocity),.velocity=velocity,
        .life=ALIVE,.contact=AIRBORNE,.pose=STANDING,.animation=MOVE_IDLE,.animation_tick=1,
        .health=0,.slots={{.id=AK74}}};
    ragdoll_start(&actor);
    actor.life=DEAD;
    return actor;
}

static Vec3 center(const Vec3 points[25]) {
    Vec3 result={0};
    for (int i=1;i<=20;i++) result=add(result,points[i]);
    return scale(result,1.0f/20);
}

int main(void) {
    world_init();poses_init();ragdolls_init();
    check(ragdoll_links[19].a==12 && ragdoll_links[19].b==9,"original head constraint");
    check(fabsf(ragdoll_links[19].length-3)<.0001f,"original head rest length");
    check(fabsf(ragdoll_links[2].length-3.5f)<.0001f,"original pelvis rest length");
    Actor actor=corpse(v3(0,10000,0),v3(1,2,3));
    Vec3 before=center(actor.ragdoll.position),velocity=sub(before,center(actor.ragdoll.old_position));
    ragdoll_step(&actor);
    Vec3 expected=add(add(before,scale(velocity,SRC_RAGDOLL_DAMPING)),v3(0,-SRC_RAGDOLL_GRAVITY,0));
    check(length(sub(center(actor.ragdoll.position),expected))<.004f,"source Verlet gravity and damping preserve center of mass");
    for (int tick=0;tick<239;tick++) ragdoll_step(&actor);
    float relative_error=0;
    for (int i=0;i<28;i++) {
        RagdollLink link=ragdoll_links[i];
        float error=fabsf(length(sub(actor.ragdoll.position[link.a],actor.ragdoll.position[link.b]))-link.length)/link.length;
        relative_error=fmaxf(relative_error,error);
    }
    printf("constraint relative error %.6f\n",relative_error);
    check(relative_error<.15f,"single source constraint pass maintains articulated lengths");
    Actor struck=corpse(v3(0,1000,0),v3(0,0,0)),control=struck;
    ragdoll_hit(&struck,12,v3(4,3,0));
    ragdoll_step(&struck);ragdoll_step(&control);
    check(length(sub(struck.ragdoll.position[12],control.ragdoll.position[12]))>1,"head impact produces body motion");
    Vec3 blast=sub(struck.ragdoll.position[12],v3(5,0,0));
    Vec3 old=struck.ragdoll.old_position[12];
    ragdoll_explosion(&struck,blast,85);
    check(fabsf(struck.ragdoll.old_position[12].x-old.x+3.75f)<.0001f,"source dead explosion impulse");
    struck.health=-90;
    ragdoll_dismember(&struck,12);
    check(struck.ragdoll.severed==(1u<<19),"head chop removes only original neck constraint");
    struck.health=-400;
    ragdoll_dismember(&struck,10);
    Vec3 wounds[10];
    check(ragdoll_wounds(&struck,wounds)==10,"brutal death exposes both ends of five original cuts");
    ragdoll_hit(&struck,12,v3(20,0,0));
    for (int tick=0;tick<30;tick++) ragdoll_step(&struck);
    check(length(sub(struck.ragdoll.position[12],struck.ragdoll.position[9]))>50,"severed head moves independently");
    Vec3 pose[21];actor_pose(&struck,pose);
    for (int i=1;i<=20;i++) check(length(sub(pose[i],struck.ragdoll.position[i]))==0,"render and collision pose consume authoritative corpse bones");
    actor=corpse(add(world_spawns[0],v3(0,35,0)),v3(0,0,0));
    int contacts=0;
    for (int tick=0;tick<1200;tick++) {
        ragdoll_step(&actor);
        if (actor.contact==GROUNDED) contacts++;
    }
    float maximum_speed=0;
    for (int i=1;i<=16;i++) {
        if (i==7 || i==8) continue;
        float radius=ragdoll_part_radius[i]*.99f;
        WorldHit hit=world_trace(actor.ragdoll.position[i],actor.ragdoll.position[i],v3(radius,radius,radius));
        if (hit.box>=0) fprintf(stderr,"embedded bone%d at%.5f,%.5f,%.5f solid%d\n",i,actor.ragdoll.position[i].x,actor.ragdoll.position[i].y,actor.ragdoll.position[i].z,hit.box);
        check(hit.box<0,"corpse collision points remain outside terrain");
        float speed=length(sub(actor.ragdoll.position[i],actor.ragdoll.old_position[i]));
        maximum_speed=fmaxf(maximum_speed,speed);
    }
    printf("terrain contacts %d, settled speed %.6f\n",contacts,maximum_speed);
    check(contacts>0,"falling corpse contacts original terrain");
    check(maximum_speed<.2f,"corpse settles on terrain");
    Game game={.max_grenades=2};
    game.actors[0]=struck;game.actors[0].loadout[0]=AK74;game.actors[0].loadout[1]=COLT;
    game_respawn(&game,0);
    check(game.actors[0].life==ALIVE && game.actors[0].ragdoll.severed==0 && game.actors[0].ragdoll.ticks==0,"respawn resets severing and body age");
    for (int i=1;i<=RAGDOLL_PART_COUNT;i++)
        check(length(game.actors[0].ragdoll.position[i])==0 && length(game.actors[0].ragdoll.old_position[i])==0 && length(game.actors[0].ragdoll.previous[i])==0,"respawn clears replicated corpse state");
    ragdolls_free();poses_free();world_free();
    puts("ragdoll contracts passed");
    return 0;
}
