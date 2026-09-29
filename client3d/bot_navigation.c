#include "bot_navigation.h"
#include "world.h"
#include "generated_rules.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

void bot_navigation(Game *game, int index, Vec3 approach, BotMoveIntent intent, Input *input) {
    Actor *actor = &game->actors[index];
    input->right = input->forward = 0;
    input->held &= ~(INPUT_JUMP | INPUT_JETS | INPUT_CROUCH | INPUT_PRONE | INPUT_ROLL);
    if (actor->life != ALIVE || game->phase != MATCH_PLAYING) return;
    BotMemory *memory = &game->bots[index];
    if (intent == BOT_HOLD && actor->contact == GROUNDED) {
        actor->nav_edge = -1;
        actor->nav_goal = -1;
    }
    int source = -1, goal = -1;
    float source_distance = INFINITY, goal_distance = INFINITY;
    Vec3 center = add(actor->position, v3(0, actor_height(actor->pose) * .5f, 0));
    WorldQuery movement_query = {WORLD_TRACE_ACTOR, actor->team,
        actor->carried_flag == FLAG_NONE ? WORLD_NO_FLAG : WORLD_HAS_FLAG};
    if (intent == BOT_ENGAGE && actor->contact == GROUNDED &&
        fabsf(approach.y - actor->position.y) < actor_height(STANDING) &&
        hypotf(approach.x - actor->position.x, approach.z - actor->position.z) < 6 * SRC_PART_RADIUS) {
        approach.y = actor->position.y;
        Vec3 side = direction(input->yaw - 1.57079632679f, 0);
        int sign = memory->nav_strafe_sign ? memory->nav_strafe_sign : (index % 2 ? -1 : 1);
        Vec3 point = add(approach, scale(side, 4 * SRC_PART_RADIUS * sign));
        if (length(sub(point, actor->position)) < SRC_PART_RADIUS) sign = -sign;
        for (int choice = 0; choice < 2; ++choice, sign = -sign) {
            point = add(approach, scale(side, 4 * SRC_PART_RADIUS * sign));
            WorldHit floor = world_trace_for(add(point, v3(0, 8, 0)), add(point, v3(0, 6, 0)),
                v3(3, 7, 3), movement_query);
            if (floor.box < 0 || floor.normal.y < .5f ||
                world_trace_for(center, add(point, v3(0, 7, 0)), v3(3, 6.8f, 3), movement_query).box >= 0) continue;
            approach = point;
            memory->nav_strafe_sign = sign;
            break;
        }
    }
    if (actor->nav_edge >= 0 && world_gate_count &&
        !world_nav_link_allows(&world_nav_links[actor->nav_edge], movement_query)) actor->nav_edge = -1;
    if (actor->nav_edge >= 0 && actor->contact == GROUNDED) {
        const NavLink *committed = &world_nav_links[actor->nav_edge];
        if (committed->mode == NAV_WALK) {
            Vec3 start = world_nav_nodes[committed->from].position;
            Vec3 route = sub(world_nav_nodes[committed->to].position, start);
            float along = dot(sub(actor->position, start), route) / dot(route, route);
            Vec3 projection = add(start, scale(route, fmaxf(0, fminf(1, along))));
            if (projection.y > actor->position.y + actor_height(actor->pose)) {
                Vec3 above = add(projection, v3(0, actor_height(STANDING), 0));
                Vec3 below = v3(projection.x, world_bounds.min.y - 8, projection.z);
                WorldHit floor = world_trace_for(above, below, v3(3, 0, 3), movement_query);
                if (floor.box >= 0 && floor.normal.y > .5f) {
                    Vec3 feet = add(add(above, scale(sub(below, above), floor.fraction)), v3(0, .05f, 0));
                    WorldHit recover = world_trace_for(center, add(feet, v3(0, 7, 0)),
                        v3(3, 6.8f, 3), movement_query);
                    if (recover.box >= 0 && recover.normal.y < -.5f) actor->nav_edge = -1;
                }
            }
        }
    }
    if (actor->nav_edge >= 0) source = world_nav_links[actor->nav_edge].from;
    WorldHit local_floor = {.box = -1};
    if ((intent == BOT_ENGAGE || intent == BOT_HOLD) && actor->contact == GROUNDED &&
        length(sub(approach, actor->position)) < 6 * SRC_PART_RADIUS)
        local_floor = world_trace_for(add(approach, v3(0, 8, 0)), add(approach, v3(0, 6, 0)),
            v3(3, 7, 3), movement_query);
    int local_ground = local_floor.box >= 0 && local_floor.normal.y >= .5f &&
        world_trace_for(center, add(approach, v3(0, 7, 0)), v3(3, 6.8f, 3), movement_query).box < 0;
    if (local_ground) actor->nav_edge = -1;
    if (actor->nav_edge >= 0) {
        const NavLink *committed = &world_nav_links[actor->nav_edge];
        Vec3 remaining = sub(world_nav_nodes[committed->to].position, actor->position);
        if (length(remaining) < 12 && fabsf(remaining.y) < 8) {
            source = committed->to;
            actor->nav_edge = -1;
        } else source = committed->from;
    }
    if (actor->nav_edge < 0) {
        int find_source = source < 0;
        for (size_t node = 0; node < world_nav_node_count; ++node) {
            if (world_gate_count && !world_nav_link_allows(
                &(NavLink){.from=(int)node,.to=(int)node,.mode=NAV_WALK},movement_query)) continue;
            Vec3 point = world_nav_nodes[node].position;
            Vec3 to_actor = sub(point, actor->position), to_goal = sub(point, approach);
            float score = length(to_actor) + fabsf(to_actor.y) * 2;
            if (find_source && score < source_distance) {
                if (world_occluded_for(center, add(point, v3(0, 7, 0)), movement_query))
                    score += length(sub(world_bounds.max, world_bounds.min));
                if (score < source_distance) { source_distance = score; source = (int)node; }
            }
            score = dot(to_goal, to_goal) + to_goal.y * to_goal.y * 4;
            if (score < goal_distance) { goal_distance = score; goal = (int)node; }
        }
    }
    assert(source >= 0 && (actor->nav_edge >= 0 || goal >= 0));
    if (actor->nav_edge < 0 && source != goal && !local_ground &&
        !(intent == BOT_HOLD && actor->contact == GROUNDED)) {
        float costs[world_nav_node_count];
        int next[world_nav_node_count], first[world_nav_node_count], incoming[world_nav_link_count];
        int heap[world_nav_node_count], positions[world_nav_node_count];
        for (size_t node = 0; node < world_nav_node_count; ++node) {
            costs[node] = (int)node == goal ? 0 : INFINITY;
            next[node] = -1;
            first[node] = positions[node] = -1;
        }
        for(size_t link=world_nav_link_count;link-- >0;) {
            int destination=world_nav_links[link].to;
            incoming[link]=first[destination];first[destination]=(int)link;
        }
        size_t queued=1;
        heap[0]=goal;positions[goal]=0;
        while(queued) {
            int closest=heap[0];
            if(closest==source)break;
            float cheapest=costs[closest];
            positions[closest]=-2;
            int replacement=heap[--queued];
            if(queued) {
                size_t parent=0;
                while(parent*2+1<queued) {
                    size_t child=parent*2+1;
                    if(child+1<queued && (costs[heap[child+1]]<costs[heap[child]] ||
                        (costs[heap[child+1]]==costs[heap[child]] && heap[child+1]<heap[child])))++child;
                    if(costs[replacement]<costs[heap[child]] ||
                        (costs[replacement]==costs[heap[child]] && replacement<heap[child]))break;
                    heap[parent]=heap[child];positions[heap[parent]]=(int)parent;parent=child;
                }
                heap[parent]=replacement;positions[replacement]=(int)parent;
            }
            for(int link=first[closest];link>=0;link=incoming[link]) {
                const NavLink *edge = &world_nav_links[link];
                if (edge->fuel > actor->fuel_capacity) continue;
                float wait = fmaxf(0, (float)(edge->fuel - actor->fuel));
                float preference = 1 + .08f * (float)((edge->from * 3 + index) % 5);
                float cost = cheapest + edge->cost * preference + wait;
                if (cost < costs[edge->from]) {
                    if (world_gate_count && !world_nav_link_allows(edge,movement_query)) continue;
                    costs[edge->from] = cost;
                    next[edge->from] = (int)link;
                    int position=positions[edge->from];
                    assert(position!=-2);
                    if(position<0)position=(int)queued++;
                    while(position>0) {
                        int parent=(position-1)/2,node=heap[parent];
                        if(costs[node]<cost || (costs[node]==cost && node<edge->from))break;
                        heap[position]=node;positions[node]=position;position=parent;
                    }
                    heap[position]=edge->from;positions[edge->from]=position;
                }
            }
        }
        if (next[source] < 0) {
            fprintf(stderr, "No bot route on %s from node %d to %d for actor %d with %d jet capacity\n",
                world_map_names[world_map_current], source, goal, index, actor->fuel_capacity);
            abort();
        }
        actor->nav_goal = goal;
        actor->nav_edge = next[source];
        actor->nav_phase = BOT_ASCEND;
    }
    const NavLink *edge = actor->nav_edge < 0 ? NULL : &world_nav_links[actor->nav_edge];
    if (edge && edge->mode == NAV_DROP && actor->nav_phase == BOT_ASCEND) actor->nav_phase = BOT_CROSS;
    Vec3 waypoint = world_nav_nodes[edge ? edge->to : goal].position;
    if (!edge && length(sub(approach, waypoint)) < 6 * SRC_PART_RADIUS && fabsf(approach.y - waypoint.y) < 4 &&
        world_trace_for(center, add(approach, v3(0, 7, 0)), v3(3, 6.8f, 3), movement_query).box < 0)
        waypoint = approach;
    if (local_ground) waypoint = approach;
    if (intent == BOT_HOLD && actor->contact == GROUNDED) waypoint = actor->position;
    Vec3 destination = waypoint;
    WorldHit corridor = world_trace_for(center, add(waypoint, v3(0, 7, 0)), v3(3, 6.8f, 3), movement_query);
    int route_recovery = edge && edge->mode == NAV_WALK && corridor.box >= 0 && corridor.normal.y < .5f &&
        length(sub(actor->position, world_nav_nodes[source].position)) > 12;
    if (route_recovery) {
        WorldHit planned_corridor = world_trace_for(add(world_nav_nodes[source].position, v3(0, 7, 0)),
            add(waypoint, v3(0, 7, 0)), v3(3, 6.8f, 3), movement_query);
        route_recovery = planned_corridor.box < 0 || planned_corridor.normal.y >= .5f;
    }
    if (route_recovery) {
        Vec3 origin = world_nav_nodes[source].position;
        Vec3 route = sub(waypoint, origin);
        float along = dot(sub(actor->position, origin), route) / dot(route, route);
        waypoint = add(origin, scale(route, fmaxf(0, fminf(1, along))));
    }
    Vec3 travel = sub(waypoint, actor->position);
    float distance = sqrtf(travel.x * travel.x + travel.z * travel.z);
    int flight = edge && edge->mode != NAV_WALK;
    if (flight) {
        int jump_thrust = actor->animation == MOVE_JUMP && actor->animation_tick < 15;
        int crossing_clear = edge->mode == NAV_JET && !jump_thrust &&
            actor->position.y > waypoint.y + actor_height(STANDING) &&
            world_trace_for(center, v3(waypoint.x, center.y, waypoint.z),
                v3(3, 6.8f, 3), movement_query).box < 0;
        if (actor->nav_phase == BOT_ASCEND && (actor->position.y >= edge->apex - 1 || crossing_clear))
            actor->nav_phase = BOT_CROSS;
        if (actor->nav_phase == BOT_CROSS && distance < 14)
            actor->nav_phase = BOT_LAND;
    }
    float flight_height = flight && actor->nav_phase != BOT_LAND
        ? (edge->mode == NAV_DROP ? world_nav_nodes[edge->from].position.y : edge->apex) : waypoint.y;
    Vec3 planar_velocity = v3(actor->velocity.x, 0, actor->velocity.z);
    float ground_damping = SRC_EDAMPING * SRC_SURFACECOEFX;
    float run_speed = SRC_RUNSPEED * ground_damping / (1 - ground_damping);
    Vec3 steer = v3(travel.x, 0, travel.z);
    if (actor->contact == AIRBORNE) {
        Vec3 velocity = v3(actor->velocity.x, 0, actor->velocity.z);
        steer = sub(steer, scale(velocity, length(velocity) / (2 * SRC_FLYSPEED)));
    }
    float steer_distance = length(steer);
    Vec3 desired = v3(0, 0, 0);
    if (steer_distance > (route_recovery ? 0 : 1)) {
        float speed = flight ? 2.8f : run_speed;
        if (!flight && actor->contact == AIRBORNE) speed = fmaxf(speed, length(planar_velocity));
        desired = scale(steer, fminf(speed, steer_distance * .14f) / steer_distance);
    }
    memory->nav_neighbors = 0;
    if (!flight && game_team_mode(game->mode)) {
        Vec3 separation = v3(0, 0, 0);
        for (int other = 0; other < ACTOR_COUNT; ++other) {
            const Actor *neighbor = &game->actors[other];
            if (other == index || neighbor->life != ALIVE || neighbor->team != actor->team ||
                fabsf(neighbor->position.y - actor->position.y) >= actor_height(STANDING)) continue;
            Vec3 apart = sub(actor->position, neighbor->position);
            apart.y = 0;
            float spacing = length(apart);
            if (spacing >= 3 * SRC_PART_RADIUS) continue;
            memory->nav_neighbors += spacing < 2 * SRC_PART_RADIUS;
            Vec3 away = spacing > 0 ? scale(apart, 1 / spacing) : v3(index < other ? -1 : 1, 0, 0);
            separation = add(separation, scale(away, run_speed * (1 - spacing / (3 * SRC_PART_RADIUS))));
        }
        WorldHit escape = world_trace_for(center, add(center, scale(separation, actor_height(STANDING))),
            v3(3, 6.8f, 3), movement_query);
        if (escape.box >= 0 && escape.normal.y < .5f)
            separation = sub(separation, scale(escape.normal, fminf(0, dot(separation, escape.normal))));
        desired = add(desired, separation);
        float speed = length(desired);
        if (speed > run_speed) desired = scale(desired, run_speed / speed);
    }
    uint64_t threat = 0;
    Vec3 threat_velocity = v3(0, 0, 0);
    float impact_time = sqrtf(4 * SRC_PART_RADIUS / SRC_RUNSPEED);
    if (!flight) for (size_t i = 0; i < game->projectile_count; ++i) {
        const Projectile *projectile = &game->projectiles[i];
        if (projectile->owner == index || (game_team_mode(game->mode) && !game->friendly_fire &&
            projectile->owner >= 0 && game->actors[projectile->owner].team == actor->team)) continue;
        Vec3 relative = sub(add(actor->position, v3(0, 12, 0)), projectile->position);
        Vec3 velocity = sub(projectile->velocity, actor->velocity);
        float speed_squared = dot(velocity, velocity);
        if (speed_squared == 0) continue;
        float time = dot(relative, velocity) / speed_squared;
        if (time <= 0 || time >= impact_time) continue;
        Vec3 miss = sub(relative, scale(velocity, time));
        if (dot(miss, miss) > 4 * SRC_PART_RADIUS * SRC_PART_RADIUS) continue;
        WorldHit cover = world_trace_for(projectile->position,
            add(projectile->position, scale(projectile->velocity, time)), v3(0, 0, 0),
            (WorldQuery){WORLD_TRACE_BULLET, actor->team, WORLD_NO_FLAG});
        if (cover.box >= 0) continue;
        threat = projectile->id;
        threat_velocity = velocity;
        impact_time = time;
    }
    if (threat) {
        if (memory->nav_dodge_projectile != threat) {
            Vec3 side = v3(-threat_velocity.z, 0, threat_velocity.x);
            float side_length = length(side);
            if (side_length > 0) side = scale(side, 1 / side_length);
            if (dot(side, add(planar_velocity, desired)) < 0) side = scale(side, -1);
            if (world_trace_for(center, add(center, scale(side, 2 * SRC_PART_RADIUS)),
                v3(3, 6.8f, 3), movement_query).box >= 0) side = scale(side, -1);
            if (world_trace_for(center, add(center, scale(side, 2 * SRC_PART_RADIUS)),
                v3(3, 6.8f, 3), movement_query).box >= 0) side = v3(0, 0, 0);
            memory->nav_dodge_direction = side;
            memory->nav_dodge_projectile = threat;
            if (intent != BOT_HOLD && actor->contact == GROUNDED &&
                actor->animation != MOVE_ROLL && actor->animation != MOVE_ROLLBACK) {
                float roll_distance = SRC_ROLLSPEED * ground_damping / (1 - ground_damping) * SRC_ROLL_FRAMES;
                if (length(side) > 0 && world_trace_for(center, add(center, scale(side, roll_distance)),
                    v3(3, 6.8f, 3), movement_query).box < 0) input->held |= INPUT_ROLL;
            }
        }
        desired = add(desired, scale(memory->nav_dodge_direction, run_speed));
        float speed = length(desired);
        if (speed > run_speed) desired = scale(desired, run_speed / speed);
    } else memory->nav_dodge_projectile = 0;
    if (edge && edge->mode == NAV_JET && actor->contact == GROUNDED && actor->fuel < edge->fuel &&
        length(sub(actor->position, world_nav_nodes[source].position)) < 16)
        desired = v3(0, 0, 0);
    if (flight && actor->nav_phase == BOT_ASCEND)
        desired = v3(0, 0, 0);
    Vec3 movement = sub(desired, planar_velocity);
    if (!flight && actor->contact == GROUNDED) {
        movement = add(scale(desired, (1 - ground_damping) / ground_damping),
            scale(movement, 1 - ground_damping));
        WorldHit support=world_trace_for(add(actor->position,v3(0,1,0)),sub(actor->position,v3(0,1,0)),
            v3(3,0,3),movement_query);
        if(support.box>=0 && support.normal.y>.5f) {
            Vec3 normal=support.normal;
            float tangent=(normal.x*movement.x+normal.z*movement.z)/(normal.y*normal.y);
            float gravity=(SRC_RUNSPEEDUP-SRC_GRAV)/normal.y;
            Vec3 along=add(movement,scale(v3(normal.x,0,normal.z),tangent));
            Vec3 hold=scale(v3(normal.x,0,normal.z),gravity);
            movement=add(along,hold);
            if(dot(movement,movement)>SRC_RUNSPEED*SRC_RUNSPEED) {
                float a=dot(along,along),b=2*dot(along,hold);
                float c=dot(hold,hold)-SRC_RUNSPEED*SRC_RUNSPEED;
                float fraction=(-b+sqrtf(b*b-4*a*c))/(2*a);
                movement=add(scale(along,fraction),hold);
            }
        }
        movement = scale(movement, 1 / SRC_RUNSPEED);
        if (length(sub(destination, actor->position)) < 2 && !threat && !memory->nav_neighbors) movement = v3(0, 0, 0);
    }
    float magnitude = length(movement);
    if (magnitude > 1) movement = scale(movement, 1 / magnitude);
    input->forward = movement.x * sinf(input->yaw) + movement.z * cosf(input->yaw);
    input->right = -movement.x * cosf(input->yaw) + movement.z * sinf(input->yaw);
    int jumping = (actor->animation == MOVE_SIDEJUMP && actor->animation_tick <= 10) ||
        (actor->animation == MOVE_JUMP && actor->animation_tick <= 14);
    int launch = edge && actor->nav_phase == BOT_ASCEND &&
        actor->velocity.x*actor->velocity.x+actor->velocity.z*actor->velocity.z < .1f &&
        (edge->mode == NAV_JUMP || (edge->mode == NAV_JET && actor->fuel >= edge->fuel));
    int step_over = 0;
    if (!edge || edge->mode == NAV_WALK) {
        Vec3 ahead = distance > 1 ? scale(v3(travel.x, 0, travel.z), fminf(18, distance) / distance) : v3(0, 0, 0);
        WorldHit wall = world_trace_for(center, add(center, ahead), v3(3, 6, 3), movement_query);
        step_over = wall.box >= 0 && wall.normal.y < .5f;
    }
    float hop_distance = (run_speed + 3.5f * SRC_JUMPDIRSPEED) * SRC_SIDEJUMP_FRAMES;
    float impulse = 7 * SRC_JUMPDIRSPEED / 1.2f;
    float hop_height = impulse * impulse / (2 * SRC_GRAV);
    int bunny = intent != BOT_HOLD && !flight && actor->contact == GROUNDED &&
        actor->pose == STANDING && fabsf(travel.y) < 1 &&
        distance > hop_distance && length(planar_velocity) > run_speed * .75f &&
        world_trace_for(center, add(center, v3(0, hop_height, 0)), v3(3, 7, 3), movement_query).box < 0 &&
        world_trace_for(add(center, v3(0, hop_height, 0)),
            add(add(center, v3(0, hop_height, 0)), scale(v3(travel.x, 0, travel.z), hop_distance / distance)),
            v3(3, 7, 3), movement_query).box < 0;
    if (bunny) {
        Vec3 landing = add(center, scale(v3(travel.x, 0, travel.z), hop_distance / distance));
        WorldHit ground = world_trace_for(add(landing, v3(0, 1, 0)), sub(landing, v3(0, 1, 0)),
            v3(3, 7, 3), movement_query);
        bunny = ground.box >= 0 && ground.normal.y > .5f;
        if (bunny) {
            unsigned type = world_solids[ground.box].poly_type;
            bunny = type != 5 && type != 6 && type != 18 && type != 19;
        }
    }
    if (jumping || (actor->contact == GROUNDED && (launch || step_over || bunny)))
        input->held |= INPUT_JUMP;
    else if (actor->contact == AIRBORNE && actor->fuel > 0 &&
        (!edge || edge->mode != NAV_JUMP) &&
        ((flight && actor->nav_phase != BOT_LAND) || waypoint.y > actor->position.y + 4) &&
        actor->position.y + actor->velocity.y * 10 < flight_height)
        input->held |= INPUT_JETS;
    if (!flight && (bunny || (jumping && actor->animation == MOVE_SIDEJUMP)) && distance > 1) {
        Vec3 forward = scale(v3(travel.x, 0, travel.z), 1 / distance);
        input->forward = forward.x * sinf(input->yaw) + forward.z * cosf(input->yaw);
        input->right = -forward.x * cosf(input->yaw) + forward.z * sinf(input->yaw);
    }
    if (flight && actor->nav_phase == BOT_ASCEND && (input->held & INPUT_JUMP))
        input->forward = input->right = 0;
    if ((input->held & INPUT_JETS) && actor->animation == MOVE_SIDEJUMP)
        input->forward = input->right = 0;
}
