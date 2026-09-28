#include "game.h"
#include "world.h"
#include "world_layouts.h"
#include "pose.h"
#include "ragdoll.h"
#include "generated_rules.h"
#include "hit_feedback.h"

#include <stdio.h>
#include <stdlib.h>

static void check(const char *contract, int pass) {
    if (pass) return;
    fprintf(stderr, "Combat contract failed: %s\n", contract);
    exit(EXIT_FAILURE);
}

static void equal(const char *contract, float actual, float expected) {
    if (fabsf(actual - expected) < 0.001f) return;
    fprintf(stderr, "%s: got %.8f, expected %.8f\n", contract, actual, expected);
    exit(EXIT_FAILURE);
}

static Game shooting_game(WeaponId weapon) {
    Game game;
    game_init(&game, 321, MODE_DEATHMATCH);
    for (int i = 0; i < ACTOR_COUNT; ++i) game.actors[i].spawn_protection_ticks = -1;
    for (int i = 1; i < ACTOR_COUNT; i++) game.actors[i].life = INACTIVE;
    game.actors[0].position = v3(0, 10000, 0);
    game.actors[0].contact = GROUNDED;
    game_equip(&game.actors[0], weapon, COLT);
    game.actors[0].slots[0].fire_count = 0;
    return game;
}

static void step(Game *game, uint32_t held, uint32_t pressed) {
    Input inputs[ACTOR_COUNT] = {0};
    inputs[0].held = held;
    inputs[0].pressed = pressed;
    game->event_count = 0;
    combat_step(game, inputs);
    game->tick++;
    combat_history_record(game);
}

int main(void) {
    GameEvent confirmations[7];
    for (int i=0;i<6;++i) confirmations[i]=(GameEvent){.kind=EVENT_HIT,.actor=0,.target=1};
    check("shotgun pellets produce one hit confirmation",hit_confirmation(confirmations,6,0)==HIT_DAMAGE);
    confirmations[6]=(GameEvent){.kind=EVENT_KILL,.actor=0,.target=1};
    check("lethal hit batch produces one kill confirmation",hit_confirmation(confirmations,7,0)==HIT_KILL);
    confirmations[0]=confirmations[6];
    check("kill confirmation survives later pellet hits",hit_confirmation(confirmations,6,0)==HIT_KILL);
    check("other players hits do not confirm local damage",hit_confirmation(confirmations,7,2)==HIT_NONE);
    check("taking hits does not confirm outgoing damage",hit_confirmation(confirmations,7,1)==HIT_NONE);
    confirmations[0]=(GameEvent){.kind=EVENT_HIT,.actor=0,.target=0};
    confirmations[1]=(GameEvent){.kind=EVENT_KILL,.actor=0,.target=0};
    check("self damage and suicide produce no confirmation",hit_confirmation(confirmations,2,0)==HIT_NONE);
    check("empty event batch produces no confirmation",hit_confirmation(NULL,0,0)==HIT_NONE);
    confirmations[0]=(GameEvent){.kind=EVENT_SHOT,.actor=0,.target=1};
    check("predicted shots do not confirm hits",hit_confirmation(confirmations,1,0)==HIT_NONE);
    world_init();
    poses_init();
    ragdolls_init();
    check("all source weapon slots generated", weapons[THROWNKNIFE].num == 53);
    equal("normal mode health imported", SRC_DEFAULT_HEALTH, 150);
    equal("normal AK speed imported", weapons[AK74].speed, 24.6f);
    check("source derived cluster definition", weapons[CLUSTER].hit_multiply == weapons[FRAGGRENADE].hit_multiply);

    Game game = shooting_game(AK74);
    game.actors[0].spawn_protection_ticks = 0;
    step(&game, INPUT_FIRE, INPUT_FIRE);
    check("last protected tick blocks firing", game.projectile_count == 0);
    game.actors[0].spawn_protection_ticks = -1;
    step(&game, INPUT_FIRE, 0);
    check("firing begins after spawn protection expires", game.projectile_count == 1);
    game_free(&game);

    game = shooting_game(MP5);
    int fired = 0;
    for (int tick = 0; tick < 61; tick++) {
        int before = game.actors[0].slots[0].ammo;
        step(&game, INPUT_FIRE, 0);
        if (game.actors[0].slots[0].ammo != before) {
            check("MP5 fires exactly every six ticks", tick == fired * weapons[MP5].fire_interval);
            fired++;
        }
    }
    check("held trigger produces eleven MP5 shots in sixty elapsed ticks", fired == 11);
    game_free(&game);

    game = shooting_game(EAGLE);
    step(&game, INPUT_FIRE, INPUT_FIRE);
    check("dual Eagles create two projectiles per ammo unit", game.projectile_count == 2 && game.actors[0].slots[0].ammo == 6);
    for (int tick = 0; tick < 60; tick++) step(&game, INPUT_FIRE, 0);
    check("semi auto requires trigger release", game.actors[0].slots[0].ammo == 6);
    step(&game, 0, 0);
    step(&game, INPUT_FIRE, INPUT_FIRE);
    check("released semi auto can fire again", game.actors[0].slots[0].ammo == 5);
    game_free(&game);

    game = shooting_game(BARRETT);
    for (int tick = 0; tick < weapons[BARRETT].startup_time; tick++) {
        step(&game, INPUT_FIRE, 0);
        check("Barrett startup cannot fire early", game.projectile_count == 0);
    }
    step(&game, INPUT_FIRE, 0);
    check("Barrett fires after source startup countdown", game.projectile_count == 1);
    game_free(&game);

    game = shooting_game(M79);
    check("M79 spawns empty", game.actors[0].slots[0].ammo == 0);
    for (int tick = 0; tick < weapons[M79].reload_time - 1 + weapons[M79].fire_interval; tick++) {
        step(&game, INPUT_FIRE, 0);
        check("M79 first shot includes source shell ejection tick skip", game.projectile_count == 0);
    }
    step(&game, INPUT_FIRE, 0);
    check("M79 fires after reload and fire interval", game.projectile_count == 1);
    game_free(&game);

    game = shooting_game(SPAS12);
    step(&game, INPUT_FIRE, INPUT_FIRE);
    check("SPAS has six pellets", game.projectile_count == 6);
    check("SPAS shot pushes shooter backward", game.actors[0].velocity.z < 0);
    for (int tick = 0; tick < weapons[SPAS12].fire_interval; tick++) step(&game, 0, 0);
    step(&game, 0, INPUT_RELOAD);
    for (int tick = 1; tick < 25; tick++) step(&game, 0, 0);
    check("SPAS inserts a shell on source animation frame fourteen", game.actors[0].slots[0].ammo == 7);
    game_free(&game);

    game = shooting_game(COLT);
    Actor *actor = &game.actors[0];
    actor->velocity = v3(2, 3, 4);
    Vec3 origin = actor_muzzle(actor);
    Vec3 expected_velocity = add(v3(0, 0, weapons[COLT].speed), scale(actor->velocity, weapons[COLT].inherited_velocity));
    expected_velocity.y -= SRC_BULLET_GRAVITY;
    Vec3 expected_position = add(origin, expected_velocity);
    step(&game, INPUT_FIRE, INPUT_FIRE);
    equal("bullet inherits shooter velocity before source gravity", game.projectiles[0].position.x, expected_position.x);
    equal("bullet position integrates before damping", game.projectiles[0].position.z, expected_position.z);
    equal("bullet damping follows position integration", game.projectiles[0].velocity.z, expected_velocity.z * SRC_BULLET_DAMPING);
    equal("bullet vertical gravity", game.projectiles[0].velocity.y, expected_velocity.y * SRC_BULLET_DAMPING);
    game_free(&game);

    game = shooting_game(LAW);
    for (int tick = 0; tick < 60; tick++) step(&game, INPUT_FIRE, 0);
    check("LAW cannot fire standing", game.projectile_count == 0);
    game.actors[0].pose = CROUCHING;
    game.actors[0].animation_tick = 14;
    for (int tick = 0; tick <= weapons[LAW].startup_time; tick++) step(&game, INPUT_FIRE, 0);
    check("LAW fires after crouched grounded startup", game.projectile_count == 1);
    game_free(&game);

    game = shooting_game(COLT);
    game.actors[0].grenades = 3;
    for (int tick = 0; tick < 36; tick++) step(&game, INPUT_GRENADE, tick == 0 ? INPUT_GRENADE : 0);
    check("full grenade charge throws one grenade", game.projectile_count == 1 && game.actors[0].grenades == 2);
    check("source grenade fuse starts at throw", game.projectiles[0].ticks == SRC_GRENADE_TIMEOUT - 1);
    for (int tick = 1; tick < SRC_GRENADE_TIMEOUT; tick++) step(&game, INPUT_GRENADE, 0);
    check("grenade explodes on source fuse tick", game.projectile_count == 0);
    check("grenade fuse creates explosion event", game.event_count == 1 && game.events[0].kind == EVENT_EXPLOSION);
    check("holding grenade does not throw repeatedly", game.actors[0].grenades == 2);
    game_free(&game);

    for (int region = 0; region < 2; region++) {
        game = shooting_game(COLT);
        Actor *target = &game.actors[1];
        target->life = ALIVE;
        target->health = SRC_DEFAULT_HEALTH;
        target->position = v3(0, 10000, 40);
        Vec3 bones[21];
        actor_pose(target,bones);
        Vec3 point = add(bones[region == 0 ? 12 : 4],v3(0,region == 0 ? 4 : -3,0));
        game.projectiles = malloc(sizeof(*game.projectiles));
        check("allocate damage contract projectile", game.projectiles != NULL);
        game.projectile_capacity = game.projectile_count = 1;
        game.projectiles[0] = (Projectile){.position = sub(point, v3(0, 0, 12)),
            .initial = sub(point, v3(0, 0, 12)), .velocity = {0, 0, 18},
            .weapon = COLT, .owner = 0, .ticks = SRC_BULLET_TIMEOUT, .hit_multiply = weapons[COLT].hit_multiply};
        step(&game, 0, 0);
        equal("hit damage uses current speed and body region", target->health,
            SRC_DEFAULT_HEALTH - 18 * weapons[COLT].hit_multiply *
            (region == 0 ? weapons[COLT].modifier_head : weapons[COLT].modifier_legs));
        game_free(&game);
    }

    for (int scenario = 0; scenario < 6; ++scenario) {
        game = shooting_game(COLT);
        game.actors[0].position.x = 1000;
        Actor *target = &game.actors[1];
        target->life = ALIVE;
        target->health = SRC_DEFAULT_HEALTH;
        target->position = v3(0, 10000, 0);
        target->contact = GROUNDED;
        game_equip(target, AK74, COLT);
        Vec3 start = v3(-15, scenario == 1 ? 10012.5f : 10012.7f,
            scenario == 1 ? 8.1f : scenario == 2 ? 10.5f : 8.9f);
        Vec3 bones[21];
        actor_pose(target, bones);
        const int source_bones[] = {12, 11, 10, 6, 5, 4, 3};
        for (size_t part = 0; part < sizeof(source_bones) / sizeof(*source_bones); ++part) {
            Vec3 offset = sub(start, bones[source_bones[part]]);
            check("limb regression ray lies outside the seven source spheres",
                offset.y * offset.y + offset.z * offset.z > SRC_PART_RADIUS * SRC_PART_RADIUS);
        }
        Vec3 reticle = combat_aim_target(&game, 0, start, add(start, v3(30, 0, 0)), NULL);
        check("reticle hits visible limbs but not space beyond the hand",
            scenario == 2 ? reticle.x == start.x + 30 : reticle.x < 0);
        if (scenario >= 3) {
            combat_history_enable(&game);
            for (int tick = 0; tick < 10; ++tick) step(&game, 0, 0);
            target->position.z += 60;
            target->pitch = 1;
            if (scenario == 5) ++target->spawn_id;
        }
        game.projectiles = malloc(sizeof(*game.projectiles));
        check("allocate visible limb contract projectile", game.projectiles != NULL);
        game.projectile_capacity = game.projectile_count = 1;
        game.projectiles[0] = (Projectile){.position = start, .initial = start,
            .velocity = {18, 0, 0}, .weapon = COLT, .owner = 0,
            .ticks = SRC_BULLET_TIMEOUT, .hit_multiply = weapons[COLT].hit_multiply,
            .rewind = scenario >= 4 ? (Rewind){REWIND_RENDERED,1,2,11,.5f} : (Rewind){0}};
        step(&game, 0, 0);
        int hit = scenario < 2 || scenario == 4;
        equal("physical limb hits use chest damage and historical generation guards", target->health,
            SRC_DEFAULT_HEALTH - (hit ? 18 * weapons[COLT].hit_multiply * weapons[COLT].modifier_chest : 0));
        game_free(&game);
    }

    game = shooting_game(COLT);
    Actor displayed = game.actors[0];
    game_equip(&displayed, AK74, COLT);
    game.actors[0].position.x = 1000;
    game.actors[1] = displayed;
    game.actors[1].throw_frame = 1;
    Vec3 shown_poses[ACTOR_COUNT][21];
    actor_pose_between(&displayed, &game.actors[1], .5f, shown_poses[1]);
    Vec3 hand_ray = add(shown_poses[1][15], v3(0, -.4f, .2f));
    Vec3 hand_start = v3(-40, hand_ray.y, hand_ray.z), hand_end = v3(40, hand_ray.y, hand_ray.z);
    Vec3 latest = combat_aim_target(&game, 0, hand_start, hand_end, NULL);
    Vec3 shown = combat_aim_target(&game, 0, hand_start, hand_end, (const Vec3 (*)[21])shown_poses);
    equal("latest discrete throw pose does not cover the displayed hand", latest.x, hand_end.x);
    check("crosshair selects the interpolated hand actually displayed", shown.x < 0 && shown.x > -2);
    game_free(&game);

    for (int scenario = 0; scenario < 7; ++scenario) {
        game = shooting_game(COLT);
        Actor *target = &game.actors[1];
        *target = game.actors[0];
        target->position = v3(0, 1000, 0);
        target->velocity = v3(0, 0, 0);
        target->yaw = target->pitch = 0;
        target->animation = MOVE_IDLE;
        target->animation_tick = 1;
        game_equip(target, AK74, COLT);
        combat_history_enable(&game);
        Actor before = *target, after = *target;
        for (int tick = 1; tick <= 10; ++tick) {
            game.tick = (uint64_t)tick;
            target->grenade_charge = tick <= 5 ? tick - 1 : 4;
            target->spawn_protection_ticks = scenario == 6 && tick == 1 ? 0 : -1;
            if (tick == 1) before = *target;
            if (tick == 5) after = *target;
            combat_history_record(&game);
        }
        Vec3 poses[ACTOR_COUNT][21] = {0};
        actor_pose_between(&before, &after, .5f, poses[1]);
        Vec3 ray = v3(-20, 1014.625f, scenario == 3 ? 27 : 7);
        target->grenade_charge = 2;
        Vec3 displayed_hit = combat_aim_target(&game, 0, ray, add(ray, v3(40, 0, 0)),
            (const Vec3 (*)[21])poses);
        Vec3 intermediate_hit = combat_aim_target(&game, 0, ray, add(ray, v3(40, 0, 0)), NULL);
        check("recorded snapshot interpolation covers the demonstrated forearm ray",
            scenario == 3 ? displayed_hit.x == 20 : displayed_hit.x < 0);
        equal("the intermediate server animation misses the demonstrated ray", intermediate_hit.x, 20);
        target->position.z += 60;
        if (scenario == 4) ++target->spawn_id;
        if (scenario == 5) { ragdoll_start(target); target->life = DEAD; target->health = -1; }
        float health = target->health;
        game.projectiles = malloc(sizeof(*game.projectiles));
        check("allocate rendered bracket projectile", game.projectiles != NULL);
        game.projectile_count = game.projectile_capacity = 1;
        game.projectiles[0] = (Projectile){.position = ray, .initial = ray, .velocity = {40, 0, 0},
            .weapon = COLT, .owner = 0, .ticks = SRC_BULLET_TIMEOUT,
            .hit_multiply = weapons[COLT].hit_multiply,
            .rewind = scenario == 2 ? (Rewind){0} : scenario == 1 ?
                (Rewind){REWIND_RENDERED,3,3,11,1} : (Rewind){REWIND_RENDERED,1,5,11,.5f}};
        step(&game, 0, 0);
        equal("physical rewind hits the displayed limb while preserving miss, protection and generation boundaries",
            target->health, health - (scenario == 0 ? 40 * weapons[COLT].hit_multiply * weapons[COLT].modifier_chest : 0));
        if (scenario == 0) {
            check("rendered limb hit retains a physical penetrating projectile", game.projectile_count == 1);
            equal("physical limb collision matches the rendered ray intersection",
                game.projectiles[0].position.x, displayed_hit.x + 30);
        }
        game_free(&game);
    }

    game = shooting_game(BARRETT);
    game.actors[1] = game.actors[0];
    game.actors[1].position = v3(60, 1000, 120);
    combat_history_enable(&game);
    for (int tick = 1; tick <= 10; ++tick) {
        game.tick = (uint64_t)tick;
        game.actors[1].position.x = tick == 6 ? 0 : 60;
        combat_history_record(&game);
    }
    game.projectiles = malloc(sizeof(*game.projectiles));
    check("allocate continuing flight projectile", game.projectiles != NULL);
    game.projectile_count = game.projectile_capacity = 1;
    Vec3 flight_pose[21];
    actor_pose(&game.actors[1], flight_pose);
    game.projectiles[0] = (Projectile){.position = {0,flight_pose[11].y,0},
        .velocity = {0,0,40}, .weapon = BARRETT, .owner = 0, .ticks = SRC_BULLET_TIMEOUT,
        .hit_multiply = weapons[BARRETT].hit_multiply, .rewind = {REWIND_RENDERED,3,5,11,.5f}};
    for (int tick = 0; tick < 2; ++tick) {
        step(&game, 0, 0);
        equal("rewound projectile keeps physical flight time through the snapshot endpoint",
            game.actors[1].health, SRC_DEFAULT_HEALTH);
    }
    step(&game, 0, 0);
    check("flight continues through the next authoritative pose after the accepted bracket",
        game.actors[1].life == DEAD);
    game_free(&game);

    game = shooting_game(COLT);
    game.actors[1] = game.actors[0];
    game.actors[1].position = v3(0, 1000, 0);
    combat_history_enable(&game);
    for (int tick = 1; tick <= 190; ++tick) {
        game.tick = (uint64_t)tick;
        combat_history_record(&game);
    }
    actor_pose(&game.actors[1], flight_pose);
    game.actors[1].position.z += 60;
    game.projectiles = malloc(sizeof(*game.projectiles));
    check("allocate oldest accepted bracket projectile", game.projectiles != NULL);
    game.projectile_count = game.projectile_capacity = 1;
    game.projectiles[0] = (Projectile){.position = {-20,flight_pose[11].y,flight_pose[11].z},
        .velocity = {40,0,0}, .weapon = COLT, .owner = 0, .ticks = SRC_BULLET_TIMEOUT - 65,
        .hit_multiply = weapons[COLT].hit_multiply, .rewind = {REWIND_RENDERED,1,100,126,.25f}};
    step(&game, 0, 0);
    check("accepted snapshot endpoints survive until physical flight leaves their bracket",
        game.actors[1].health < SRC_DEFAULT_HEALTH);
    game_free(&game);

    game = shooting_game(KNIFE);
    for (int tick = 0; tick < 10; tick++) {
        step(&game, INPUT_FIRE, 0);
        check("knife waits for animation frame eleven", game.event_count == 0);
    }
    step(&game, INPUT_FIRE, 0);
    check("knife strikes on source animation frame eleven", game.event_count == 1 && game.events[0].kind == EVENT_SHOT);
    check("knife melee does not consume ammunition", game.actors[0].slots[0].ammo == 1);
    game_free(&game);

    game = shooting_game(COLT);
    game.actors[1].life = ALIVE;
    game.actors[1].position = v3(0, 10000, 50);
    Vec3 aim_bones[21];
    actor_pose(&game.actors[1],aim_bones);
    Vec3 aim_start = sub(aim_bones[12],v3(0,0,50));
    Vec3 target_point = combat_aim_target(&game, 0, aim_start, add(aim_start, v3(0, 0, 200)), NULL);
    equal("shoulder camera reticle selects animated head surface", target_point.z, aim_bones[12].z - SRC_PART_RADIUS);
    game_free(&game);

    world_load(world_map_index("ctf_Ash"));
    game = shooting_game(COLT);
    const LayoutRoom *hall=&world_layout("ctf_Ash")->rooms[0];
    Vec3 wall_probe=v3(hall->x,hall->y+hall->roof*.5f,hall->z-hall->depth*.5f-30);
    WorldHit wall=world_trace(wall_probe,add(wall_probe,v3(0,0,10000)),v3(0,0,0));
    check("enclosed arena provides solid cover",wall.box>=0);
    Vec3 wall_point=add(wall_probe,v3(0,0,10000*wall.fraction));
    game.actors[0].position = sub(wall_point,v3(0,10,9));
    Vec3 blocked_muzzle = actor_muzzle(&game.actors[0]);
    check("barrel origin stays in front of adjacent cover", blocked_muzzle.z <= wall_point.z+.001f);
    game.actors[1].life = ALIVE;
    game.actors[1].health = SRC_DEFAULT_HEALTH;
    game.actors[1].position = add(wall_point,v3(0,-10,40));
    step(&game, INPUT_FIRE, INPUT_FIRE);
    check("shot from adjacent cover hits the wall", game.projectile_count == 0);
    equal("adjacent cover protects actor behind barrel tip", game.actors[1].health, SRC_DEFAULT_HEALTH);
    game_free(&game);

    game = shooting_game(COLT);
    game.actors[0].position.x = 1000;
    game.actors[1].life = ALIVE;
    game.actors[1].health = SRC_DEFAULT_HEALTH;
    game.actors[1].position = v3(0, 10000, 0);
    game.projectiles = malloc(sizeof(*game.projectiles));
    check("allocate explosion contract projectile", game.projectiles != NULL);
    game.projectile_capacity = game.projectile_count = 1;
    game.projectiles[0] = (Projectile){.position = {0, 9980, 0}, .initial = {0, 9980, 0},
        .velocity = {1, 0, 0}, .weapon = FRAGGRENADE, .owner = 0,
        .ticks = 1, .hit_multiply = weapons[FRAGGRENADE].hit_multiply};
    Vec3 explosion_bones[21];
    actor_pose(&game.actors[1],explosion_bones);
    Vec3 explosion_push = sub(explosion_bones[4],v3(0,9980,0));
    float explosion_distance = length(explosion_push);
    step(&game, 0, 0);
    equal("explosion damage follows source inverse distance law", game.actors[1].health,
          SRC_DEFAULT_HEALTH - weapons[FRAGGRENADE].hit_multiply / (explosion_distance + 1));
    equal("grenade jump retains doubled vertical impulse", game.actors[1].velocity.y,
          2 * explosion_push.y * SRC_EXPLOSION_IMPACT_MULTIPLY / (explosion_distance + 1));
    game_free(&game);

    game = shooting_game(BARRETT);
    for (int i = 1; i <= 2; i++) {
        game.actors[i].life = ALIVE;
        game.actors[i].health = SRC_DEFAULT_HEALTH;
        game.actors[i].position = v3(0, 10000, 15 + 25 * i);
    }
    game.projectiles = malloc(sizeof(*game.projectiles));
    check("allocate penetration contract projectile", game.projectiles != NULL);
    game.projectile_capacity = game.projectile_count = 1;
    Vec3 penetration_bones[21];
    actor_pose(&game.actors[1],penetration_bones);
    Vec3 penetration_start=v3(penetration_bones[12].x,penetration_bones[12].y,20);
    game.projectiles[0] = (Projectile){.position = penetration_start,
        .initial = penetration_start, .velocity = {0, 0, 55}, .weapon = BARRETT,
        .owner = 0, .ticks = SRC_BULLET_TIMEOUT, .hit_multiply = weapons[BARRETT].hit_multiply};
    step(&game, 0, 0);
    check("one projectile can pierce multiple actors in one source tick",
          game.actors[1].life == DEAD && game.actors[2].life == DEAD && game.actors[0].kills == 2);
    equal("penetration retains source velocity attenuation", game.projectiles[0].velocity.z,
          55 * 0.75f * 0.75f * SRC_BULLET_DAMPING);
    game_free(&game);

    for (int scenario = 0; scenario < 3; ++scenario) {
        game = shooting_game(BARRETT);
        combat_history_enable(&game);
        game.actors[1].life = ALIVE;
        game.actors[1].health = SRC_DEFAULT_HEALTH;
        game.actors[1].position = v3(0, 10000, 120);
        game.actors[1].spawn_id = 1;
        for (int tick = 0; tick < 10; ++tick) step(&game, 0, 0);
        if (scenario == 2) ++game.actors[1].spawn_id;
        else game.actors[1].position.x = 60;
        game.actors[0].slots[0].startup_count = 0;
        Input shot[ACTOR_COUNT] = {0};
        shot[0] = (Input){.held = INPUT_FIRE, .pressed = INPUT_FIRE, .rewind = scenario ? (Rewind){REWIND_RENDERED,2,2,11,1} : (Rewind){0}};
        combat_step(&game, shot);
        ++game.tick;
        check("rewound bullets still require physical flight time", game.actors[1].health == SRC_DEFAULT_HEALTH);
        check("physical shot carries source history delay", game.projectile_count == 1 &&
            game.projectiles[0].rewind.mode == shot[0].rewind.mode &&
            game.projectiles[0].rewind.before_tick == shot[0].rewind.before_tick &&
            game.projectiles[0].rewind.applied_tick == shot[0].rewind.applied_tick);
        for (int tick = 0; tick < 5; ++tick) step(&game, 0, 0);
        if (scenario == 1)
            check("150ms view delay registers on the moving target's historical body", game.actors[1].life == DEAD);
        else check(scenario ? "history cannot damage a new respawn generation" : "uncompensated delayed aim misses the moved target",
            game.actors[1].health == SRC_DEFAULT_HEALTH);
        game_free(&game);
    }

    game = shooting_game(BARRETT);
    game.actors[1].life = ALIVE;
    game.actors[1].health = SRC_DEFAULT_HEALTH;
    game.actors[1].position = v3(0, 10000, 15);
    game.actors[0].slots[0].startup_count = 0;
    step(&game, INPUT_FIRE, INPUT_FIRE);
    check("a long barrel cannot spawn past an adjacent opponent", game.actors[1].life == DEAD);
    game_free(&game);

    game = shooting_game(AK74);
    combat_history_enable(&game);
    game.actors[0].position = sub(wall_point,v3(0,10,9));
    game.actors[1].life = ALIVE;
    game.actors[1].health = SRC_DEFAULT_HEALTH;
    game.actors[1].position = add(wall_point,v3(0,-10,40));
    for (int tick = 0; tick < 10; ++tick) step(&game, 0, 0);
    Input covered[ACTOR_COUNT] = {0};
    covered[0] = (Input){.held = INPUT_FIRE, .pressed = INPUT_FIRE, .rewind = {REWIND_RENDERED,2,2,11,1}};
    combat_step(&game, covered);
    check("historical hit tests never bypass solid world cover", game.projectile_count == 0 && game.actors[1].health == SRC_DEFAULT_HEALTH);
    game_free(&game);

    ragdolls_free();
    poses_free();
    world_free();
    puts("combat contracts passed");
    return EXIT_SUCCESS;
}
