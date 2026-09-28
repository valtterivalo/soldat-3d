#include "bot_navigation.h"
#include "generated_rules.h"
#include "pose.h"
#include "ragdoll.h"
#include "world.h"

#include <stdio.h>
#include <stdlib.h>

static void check(int condition, const char *contract) {
    if (condition) return;
    fprintf(stderr, "Bot navigation failed: %s\n", contract);
    exit(EXIT_FAILURE);
}

int main(void) {
    world_init(); poses_init(); ragdolls_init();
    Game game;
    game_init(&game, 789, MODE_DEATHMATCH);
    for (int i = 1; i < ACTOR_COUNT; ++i) game.actors[i].life = INACTIVE;
    Actor *actor = &game.actors[0];
    Vec3 center = world_nav_nodes[0].position;
    actor->position = actor->previous = center;
    actor->velocity = actor->force = v3(0, 0, 0);
    actor->contact = GROUNDED;
    for (int tick = 0; tick < TICK_RATE; ++tick) {
        Input input = {.yaw = .7f, .pitch = -.2f, .held = INPUT_FIRE | INPUT_RELOAD | INPUT_JETS};
        bot_navigation(&game, 0, actor->position, BOT_HOLD, &input);
        check(input.yaw == .7f && input.pitch == -.2f &&
            (input.held & (INPUT_FIRE | INPUT_RELOAD)) == (INPUT_FIRE | INPUT_RELOAD),
            "navigation preserves combat aim and weapon controls");
        check(input.forward == 0 && input.right == 0 && !(input.held & (INPUT_JUMP | INPUT_JETS)),
            "precision hold does not oscillate or burn fuel");
        movement_step(actor, input, game.tick++);
    }
    check(length(sub(actor->position, center)) < .1f, "ground hold remains at its original firing position");
    NavNode *saved_nodes = world_nav_nodes;
    NavLink *saved_links = world_nav_links;
    size_t saved_node_count = world_nav_node_count, saved_link_count = world_nav_link_count;
    NavNode nodes[] = {{add(center, v3(-55, 0, 0))}, {add(center, v3(55, 0, 0))},
        {add(center, v3(-55, 0, 20))}};
    NavLink links[] = {{0, 1, NAV_WALK, 110, 0, center.y}, {0, 2, NAV_WALK, 20, 0, center.y}};
    world_nav_nodes = nodes; world_nav_node_count = 3;
    world_nav_links = links; world_nav_link_count = 2;
    actor->position = actor->previous = add(nodes[0].position, v3(10, 0, 0));
    actor->nav_edge = 0; actor->nav_goal = 1;
    Input committed = {.yaw = 0};
    bot_navigation(&game, 0, nodes[2].position, BOT_TRAVERSE, &committed);
    check(actor->nav_edge == 0 && committed.right < 0,
        "moving target cannot reverse a committed route before its junction");
    actor->position = actor->previous = nodes[0].position;
    actor->velocity = actor->force = v3(0, 0, 0);
    actor->nav_edge = -1;
    float top_speed = 0;
    for (int tick = 0; tick < 3 * TICK_RATE; ++tick) {
        Input input = {.yaw = 0};
        bot_navigation(&game, 0, nodes[1].position, BOT_TRAVERSE, &input);
        movement_step(actor, input, game.tick++);
        top_speed = fmaxf(top_speed, hypotf(actor->velocity.x, actor->velocity.z));
        check(world_pose_clear(actor->position, actor->pose), "running controller keeps the physical body clear");
    }
    check(top_speed > 2.3f && length(sub(actor->position, nodes[1].position)) < 4,
        "running reaches source speed and brakes at the destination");
    world_nav_nodes = saved_nodes; world_nav_node_count = saved_node_count;
    world_nav_links = saved_links; world_nav_link_count = saved_link_count;
    {
        NavNode junctions[6];
        for(int i=0;i<6;++i)junctions[i].position=add(center,v3(10*(float)i,0,0));
        NavLink routes[]={{0,1,NAV_WALK,3,0,0},{0,2,NAV_WALK,1,0,0},
            {1,5,NAV_WALK,3,0,0},{2,3,NAV_JET,1,10,center.y+24},
            {3,5,NAV_JET,1,10,center.y+24},{0,4,NAV_JET,1,1000,center.y+24},
            {4,5,NAV_WALK,1,0,0}};
        world_nav_nodes=junctions;world_nav_node_count=6;
        world_nav_links=routes;world_nav_link_count=sizeof(routes)/sizeof(*routes);
        for(int fuel=0;fuel<=20;fuel+=20) {
            actor->position=actor->previous=center;actor->velocity=actor->force=v3(0,0,0);
            actor->contact=GROUNDED;actor->nav_edge=-1;actor->fuel=fuel;
            Input input={0};
            bot_navigation(&game,0,junctions[5].position,BOT_TRAVERSE,&input);
            check(actor->nav_edge==(fuel ? 1 : 0),
                "route search accounts for fuel wait, rejects impossible flights, and decreases queued costs");
        }
        NavLink equal[]={{0,5,NAV_WALK,1,0,0},{0,5,NAV_WALK,1,0,0}};
        world_nav_links=equal;world_nav_link_count=2;actor->nav_edge=-1;
        Input input={0};
        bot_navigation(&game,0,junctions[5].position,BOT_TRAVERSE,&input);
        check(actor->nav_edge==0,"equal routes retain stable authored ordering");
        world_nav_nodes=saved_nodes;world_nav_node_count=saved_node_count;
        world_nav_links=saved_links;world_nav_link_count=saved_link_count;
    }
    game_free(&game);
    game_init(&game, 155, MODE_TEAMMATCH);
    for (int i = 0; i < ACTOR_COUNT; ++i) {
        game.actors[i].life = i < 4 ? ALIVE : INACTIVE;
        game.actors[i].team = TEAM_ALPHA;
        game.actors[i].position = game.actors[i].previous = center;
        game.actors[i].velocity = game.actors[i].force = v3(0, 0, 0);
        game.actors[i].contact = GROUNDED;
    }
    for (int tick = 0; tick < 2 * TICK_RATE; ++tick) {
        Input inputs[ACTOR_COUNT] = {0};
        for (int i = 0; i < 4; ++i) bot_navigation(&game, i, game.actors[i].position, BOT_HOLD, &inputs[i]);
        for (int i = 0; i < 4; ++i) movement_step(&game.actors[i], inputs[i], game.tick);
        ++game.tick;
    }
    for (int i = 0; i < 4; ++i)
        for (int j = i + 1; j < 4; ++j)
            check(length(sub(game.actors[i].position, game.actors[j].position)) >= 2 * SRC_PART_RADIUS,
                "teammates reaching the same firing position separate through ordinary movement");
    game_free(&game);
    float health[2], escaped = 0;
    for (int evading = 0; evading < 2; ++evading) {
        game_init(&game, 718, MODE_DEATHMATCH);
        for (int i = 1; i < ACTOR_COUNT; ++i) game.actors[i].life = INACTIVE;
        actor = &game.actors[0];
        actor->position = actor->previous = center;
        actor->velocity = actor->force = v3(0, 0, 0);
        actor->contact = GROUNDED;
        actor->spawn_protection_ticks = -1;
        Vec3 bones[21]; actor_pose(actor, bones);
        Vec3 origin = add(bones[11], v3(0, 0, 80));
        game.projectiles = malloc(sizeof(*game.projectiles));
        check(game.projectiles != NULL, "allocate incoming physical projectile");
        game.projectile_count = game.projectile_capacity = 1;
        game.projectiles[0] = (Projectile){.id = 1, .position = origin, .previous = origin,
            .initial = origin, .velocity = {0, 0, -6}, .weapon = AK74, .owner = 1,
            .ticks = weapons[AK74].timeout, .hit_multiply = weapons[AK74].hit_multiply};
        Vec3 side = {0};
        for (int tick = 0; tick < TICK_RATE / 2; ++tick) {
            Input inputs[ACTOR_COUNT] = {0};
            if (evading) bot_navigation(&game, 0, actor->position, BOT_HOLD, &inputs[0]);
            if (game.bots[0].nav_dodge_projectile) {
                Vec3 current = game.bots[0].nav_dodge_direction;
                if (length(side) > 0) check(dot(side, current) > .99f, "one incoming projectile cannot flip the evasion side");
                side = current;
            }
            movement_step(actor, inputs[0], game.tick);
            combat_step(&game, inputs);
            ++game.tick;
        }
        health[evading] = actor->health;
        if (evading) escaped = fabsf(actor->position.x - center.x);
        game_free(&game);
    }
    check(health[0] < SRC_DEFAULT_HEALTH && health[1] > health[0] && escaped > SRC_PART_RADIUS,
        "ordinary lateral inputs evade an incoming physical round that hits a stationary soldier");
    {
        NavNode deck[]={{{-110,116.05f,110}},{{80,116.05f,110}}};
        NavLink crossing={0,1,NAV_WALK,190,0,116.05f};
        saved_nodes=world_nav_nodes;saved_links=world_nav_links;
        saved_node_count=world_nav_node_count;saved_link_count=world_nav_link_count;
        world_nav_nodes=deck;world_nav_node_count=2;world_nav_links=&crossing;world_nav_link_count=1;
        game_init(&game,789,MODE_DEATHMATCH);
        for(int i=1;i<ACTOR_COUNT;++i)game.actors[i].life=INACTIVE;
        actor=&game.actors[0];actor->position=actor->previous=deck[0].position;
        float damping=SRC_EDAMPING*SRC_SURFACECOEFX;
        actor->velocity=v3(SRC_RUNSPEED*damping/(1-damping),0,0);
        actor->force=v3(0,0,0);actor->contact=GROUNDED;
        int fuel=actor->fuel,airborne=0,landed=0;float peak=actor->position.y;
        float impulse=7*SRC_JUMPDIRSPEED/1.2f;
        unsigned flight_ticks=SRC_SIDEJUMP_FRAMES+(unsigned)ceilf(2*impulse/SRC_GRAV);
        uint32_t previous=0;
        for(unsigned tick=0;tick<flight_ticks;++tick) {
            Input input={.yaw=1.57079632679f};
            bot_navigation(&game,0,deck[1].position,BOT_TRAVERSE,&input);
            if(tick==0)check((input.held&INPUT_JUMP)!=0,"a supported elevated bridge permits a running bunny hop");
            input.pressed=input.held&~previous;previous=input.held;
            movement_step(actor,input,game.tick++);
            check(world_pose_clear(actor->position,actor->pose),"bunny hopping keeps the physical body clear");
            peak=fmaxf(peak,actor->position.y);
            if(actor->contact==AIRBORNE)airborne=1;
            if(airborne && actor->contact==GROUNDED){landed=1;break;}
        }
        check(airborne && landed && peak>deck[0].position.y+10 && actor->fuel==fuel,
            "elevated running hop takes off and lands using original jump forces without jets");
        world_nav_nodes=saved_nodes;world_nav_node_count=saved_node_count;
        world_nav_links=saved_links;world_nav_link_count=saved_link_count;
        game_free(&game);
    }
    world_load(world_map_index("ctf_Ash"));
    size_t uphill=SIZE_MAX;float grade=0;
    for(size_t i=0;i<world_nav_link_count;++i) {
        const NavLink *edge=&world_nav_links[i];
        Vec3 delta=sub(world_nav_nodes[edge->to].position,world_nav_nodes[edge->from].position);
        float horizontal=hypotf(delta.x,delta.z);
        if(edge->mode==NAV_WALK && horizontal>2*SRC_PART_RADIUS && delta.y/horizontal>grade) {
            grade=delta.y/horizontal;uphill=i;
        }
    }
    check(uphill!=SIZE_MAX && grade>.5f,"authored terrain contains a sustained uphill approach");
    NavLink climb=world_nav_links[uphill];
    NavNode slope[]={{world_nav_nodes[climb.from].position},{world_nav_nodes[climb.to].position}};
    saved_nodes=world_nav_nodes;saved_links=world_nav_links;
    saved_node_count=world_nav_node_count;saved_link_count=world_nav_link_count;
    climb.from=0;climb.to=1;
    world_nav_nodes=slope;world_nav_node_count=2;world_nav_links=&climb;world_nav_link_count=1;
    game_init(&game,789,MODE_DEATHMATCH);
    for(int i=1;i<ACTOR_COUNT;++i)game.actors[i].life=INACTIVE;
    actor=&game.actors[0];actor->position=actor->previous=slope[0].position;
    actor->velocity=actor->force=v3(0,0,0);actor->contact=GROUNDED;
    float damping=SRC_EDAMPING*SRC_SURFACECOEFX,run_speed=SRC_RUNSPEED*damping/(1-damping);
    unsigned settling=(unsigned)ceilf(length(sub(slope[1].position,slope[0].position))/run_speed+
        run_speed/SRC_RUNSPEED+4/(1-damping));
    for(unsigned tick=0;tick<settling;++tick) {
        Vec3 aim=sub(slope[1].position,actor->position);Input input={.yaw=atan2f(aim.x,aim.z)};
        bot_navigation(&game,0,slope[1].position,BOT_TRAVERSE,&input);
        movement_step(actor,input,game.tick++);
    }
    check(length(sub(actor->position,slope[1].position))<4 && actor->contact==GROUNDED,
        "slope steering reaches its destination instead of balancing gravity below it");
    world_nav_nodes=saved_nodes;world_nav_node_count=saved_node_count;
    world_nav_links=saved_links;world_nav_link_count=saved_link_count;
    game_free(&game);
    printf("Bot navigation: hold, aim isolation, route commitment, running %.3f units/tick, physical evasion %.1f vs %.1f health\n",
        top_speed, health[1], health[0]);
    ragdolls_free(); poses_free(); world_free();
    return 0;
}
