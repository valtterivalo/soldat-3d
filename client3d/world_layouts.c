#include "world_layouts.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const LayoutRoom arena_ring_rooms[] = {
    {-350,0,0,104,116,0,ROCK}, {-250,-275,40,110,100,0,ROCK},
    {50,-350,80,116,100,0,ROCK}, {325,-212.5,104,100,104,0,ROCK},
    {362.5,87.5,40,100,110,0,ROCK}, {150,325,0,116,100,0,COURTYARD},
    {-200,312.5,48,104,104,0,ROCK}, {0,12.5,120,100,100,0,ROCK},
    {-87.5,-125,0,100,100,0,PIT}
};
static const LayoutLink arena_ring_links[] = {
    {0,1,NAV_WALK,56},{1,2,NAV_WALK,48},{2,3,NAV_WALK,48},
    {3,4,NAV_WALK,52},{4,5,NAV_WALK,60},{5,6,NAV_WALK,52},{6,0,NAV_WALK,52},
    {0,8,NAV_WALK,64},{5,8,NAV_WALK,56},{1,7,NAV_JET,42},
    {3,7,NAV_JET,38},{6,7,NAV_JET,38},{8,7,NAV_JET,44}
};

static const LayoutRoom arena2_trench_rooms[] = {
    {-420,-70,12,108,130,0,ROCK},{340,100,32,108,130,0,ROCK},
    {-100,-100,-16,130,102,0,PIT},{-250,-340,100,96,102,0,ROCK},
    {110,290,96,96,102,0,ROCK},{-20,110,116,230,72,0,BRIDGE},
    {210,-270,52,102,96,0,COURTYARD},{-250,180,44,102,96,0,COURTYARD},
    {-70,430,168,88,74,0,ROCK},{-20,110,-24,122,48,0,PIT}
};
static const LayoutLink arena2_trench_links[] = {
    {0,2,NAV_WALK,64},{2,1,NAV_WALK,64},{0,3,NAV_JET,52},
    {3,6,NAV_WALK,48},{6,1,NAV_WALK,52},{0,7,NAV_WALK,52},
    {7,4,NAV_WALK,48},{4,1,NAV_JET,52},{3,5,NAV_JET,42},
    {5,4,NAV_WALK,42},{2,5,NAV_JET,44},{7,8,NAV_JET,40},
    {4,8,NAV_JET,48},{2,9,NAV_WALK,60},{9,7,NAV_WALK,52},{7,5,NAV_JET,42}
};

static const LayoutRoom arena3_bridge_rooms[] = {
    {-350,0,24,110,130,0,ROCK},{350,0,24,110,130,0,ROCK},
    {-160,0,96,100,100,0,ROCK},{160,0,96,100,100,0,ROCK},
    {0,0,96,220,64,0,BRIDGE},{-90,-200,0,130,104,0,PIT},
    {110,200,0,130,104,0,PIT},{180,-220,44,100,100,0,ROCK},
    {-180,220,44,100,100,0,ROCK},{0,360,148,100,100,0,ROCK}
};
static const LayoutLink arena3_bridge_links[] = {
    {0,2,NAV_JET,48},{2,4,NAV_WALK,48},{4,3,NAV_WALK,48},{3,1,NAV_JET,48},
    {0,5,NAV_WALK,64},{5,7,NAV_WALK,52},{7,1,NAV_WALK,56},
    {0,8,NAV_WALK,56},{8,6,NAV_WALK,52},{6,1,NAV_WALK,64},
    {5,4,NAV_JET,44},{6,4,NAV_JET,44},{6,9,NAV_JET,42},
    {8,9,NAV_JET,48},{9,3,NAV_JET,36}
};

static const LayoutRoom aero_mesa_rooms[] = {
    {-300,-260,0,116,104,0,COURTYARD},{300,260,0,116,104,0,COURTYARD},
    {-240,-40,64,116,116,0,ROCK},{240,40,64,116,116,0,ROCK},
    {-230,210,140,128,100,0,ROCK},{230,-210,140,128,100,0,ROCK},
    {0,0,36,100,100,0,ROCK},{0,-280,0,180,120,72,HALL},
    {0,280,0,180,120,72,HALL},{-430,150,40,100,100,0,ROCK},
    {430,-150,40,100,100,0,ROCK}
};
static const LayoutLink aero_mesa_links[] = {
    {0,7,NAV_WALK,64},{7,10,NAV_WALK,56},{10,3,NAV_WALK,56},
    {3,1,NAV_JET,60},{1,8,NAV_WALK,64},{8,9,NAV_WALK,56},
    {9,2,NAV_WALK,56},{2,0,NAV_JET,60},{2,6,NAV_WALK,52},{6,3,NAV_WALK,52},
    {2,4,NAV_JET,48},{3,5,NAV_JET,48},{6,4,NAV_JET,40},
    {6,5,NAV_JET,40},{9,4,NAV_JET,38},{10,5,NAV_JET,38}
};

static const LayoutRoom airpirates_lighthouse_rooms[] = {
    {-360,-180,0,116,104,0,COURTYARD},{336,156,0,116,110,0,COURTYARD},
    {-312,180,24,160,160,72,HALL},{-36,-216,52,116,100,0,ROCK},
    {276,-192,84,104,100,0,ROCK},{24,156,116,100,100,0,ROCK},
    {-192,408,168,100,100,0,ROCK},{48,432,224,100,110,64,HALL},
    {330,396,152,100,100,0,ROCK},{-36,-10,0,100,100,0,PIT}
};
static const LayoutLink airpirates_lighthouse_links[] = {
    {0,2,NAV_WALK,56},{0,3,NAV_WALK,52},{3,4,NAV_WALK,42},
    {4,1,NAV_WALK,56},{1,9,NAV_WALK,64},{9,0,NAV_WALK,64},
    {2,9,NAV_WALK,52},{6,7,NAV_WALK,42},{7,8,NAV_WALK,42},
    {8,1,NAV_JET,52},{3,5,NAV_JET,40},{4,5,NAV_JET,38},
    {9,5,NAV_JET,44},{5,6,NAV_WALK,42},{5,8,NAV_WALK,42},{5,7,NAV_JET,36}
};

static const LayoutRoom lagrange_woodland_rooms[] = {
    {-368,0,0,180,170,64,HALL},{368,0,0,180,170,64,HALL},
    {-184,-253,12,110,104,0,COURTYARD},{161,-264.5,24,116,100,0,ROCK},
    {184,253,12,110,104,0,COURTYARD},{-161,264.5,24,116,100,0,ROCK},
    {0,0,0,116,100,0,PIT},{0,126.5,16,130,70,0,BRIDGE},
    {-69,-115,24,100,70,48,HALL}
};
static const LayoutLink lagrange_woodland_links[] = {
    {0,2,NAV_WALK,58},{2,3,NAV_WALK,52},{3,1,NAV_WALK,58},
    {0,5,NAV_WALK,58},{5,4,NAV_WALK,52},{4,1,NAV_WALK,58},
    {0,6,NAV_WALK,52},{6,1,NAV_WALK,52},{5,7,NAV_WALK,42},
    {7,4,NAV_WALK,42},{2,8,NAV_WALK,42},{8,3,NAV_WALK,42},
    {6,7,NAV_JUMP,38},{6,8,NAV_JUMP,36}
};

static const LayoutRoom ash_fort_rooms[] = {
    {-650,-100,40,180,210,80,HALL},{650,100,40,180,210,80,HALL},
    {-340,-270,84,108,102,0,ROCK},{340,270,84,108,102,0,ROCK},
    {-300,130,60,108,102,0,ROCK},{300,-130,60,108,102,0,ROCK},
    {0,0,-24,130,122,0,PIT},{-60,-410,152,150,120,0,BRIDGE},
    {60,410,152,150,120,0,BRIDGE},{-180,-30,160,68,68,0,ROCK},
    {180,30,160,68,68,0,ROCK}
};
static const LayoutLink ash_fort_links[] = {
    {0,2,NAV_WALK,60},{0,4,NAV_WALK,60},{1,3,NAV_WALK,60},{1,5,NAV_WALK,60},
    {2,6,NAV_WALK,64},{3,6,NAV_WALK,64},{4,6,NAV_WALK,64},{5,6,NAV_WALK,64},
    {2,7,NAV_WALK,44},{7,5,NAV_WALK,44},{4,8,NAV_WALK,44},{8,3,NAV_WALK,44},
    {2,9,NAV_JET,40},{4,9,NAV_JET,40},{3,10,NAV_JET,40},{5,10,NAV_JET,40},
    {9,10,NAV_JET,38},{6,7,NAV_JET,44},{6,8,NAV_JET,44}
};

static const LayoutRoom kampf_cavern_rooms[] = {
    {-697.5,0,0,170,200,84,HALL},{697.5,0,0,170,200,84,HALL},
    {-356.5,-263.5,36,170,160,92,HALL},{356.5,263.5,36,170,160,92,HALL},
    {-356.5,263.5,72,150,150,0,ROCK},{356.5,-263.5,72,150,150,0,ROCK},
    {0,0,0,200,190,0,COURTYARD},{0,-418.5,136,140,140,72,HALL},
    {0,418.5,136,140,140,72,HALL},{-139.5,186,84,110,100,0,ROCK},
    {139.5,-186,84,110,100,0,ROCK}
};
static const LayoutLink kampf_cavern_links[] = {
    {0,2,NAV_WALK,56},{0,4,NAV_WALK,52},{1,3,NAV_WALK,56},{1,5,NAV_WALK,52},
    {2,6,NAV_WALK,56},{3,6,NAV_WALK,56},{4,9,NAV_WALK,44},{5,10,NAV_WALK,44},
    {9,3,NAV_WALK,44},{10,2,NAV_WALK,44},{4,8,NAV_WALK,44},{8,3,NAV_WALK,44},
    {5,7,NAV_WALK,44},{7,2,NAV_WALK,44},{6,9,NAV_JET,40},{6,10,NAV_JET,40},
    {9,7,NAV_JET,36},{10,8,NAV_JET,36}
};

static const LayoutRoom voland_vault_rooms[] = {
    {-577.5,0,40,160,200,96,HALL},{577.5,0,40,160,200,96,HALL},
    {-297,-313.5,72,140,150,0,ROCK},{297,-313.5,72,140,150,0,ROCK},
    {-297,313.5,0,160,150,64,HALL},{297,313.5,0,160,150,64,HALL},
    {0,313.5,0,160,140,0,COURTYARD},{0,-313.5,144,140,100,0,BRIDGE},
    {-115.5,0,104,100,110,0,ROCK},{115.5,0,104,100,110,0,ROCK},
    {0,-643.5,48,160,130,80,HALL}
};
static const LayoutLink voland_vault_links[] = {
    {0,2,NAV_WALK,50},{1,3,NAV_WALK,50},{0,4,NAV_WALK,56},{1,5,NAV_WALK,56},
    {4,6,NAV_WALK,56},{6,5,NAV_WALK,56},{2,7,NAV_WALK,42},{7,3,NAV_WALK,42},
    {2,10,NAV_WALK,46},{10,3,NAV_WALK,46},{2,8,NAV_WALK,42},{3,9,NAV_WALK,42},
    {8,9,NAV_JET,36},{6,8,NAV_JET,38},{6,9,NAV_JET,38},{8,7,NAV_JET,36},{9,7,NAV_JET,36}
};

static const LayoutRoom outpost_assault_rooms[] = {
    {652.5,0,0,180,220,0,COURTYARD},{-522,101.5,176,160,170,88,HALL},
    {290,-246.5,24,180,140,64,HALL},{290,261,24,180,140,0,ROCK},
    {-43.5,-275.5,64,180,140,0,ROCK},{-58,290,64,180,140,76,HALL},
    {-362.5,-275.5,120,160,150,0,ROCK},{-391.5,435,120,160,150,0,ROCK},
    {-116,0,0,190,140,0,PIT},{-681.5,-203,208,120,130,0,ROCK},
    {43.5,507.5,128,130,130,0,ROCK}
};
static const LayoutLink outpost_assault_links[] = {
    {0,2,NAV_WALK,60},{0,3,NAV_WALK,60},{2,4,NAV_WALK,54},{3,5,NAV_WALK,54},
    {4,6,NAV_WALK,48},{5,7,NAV_WALK,48},{6,1,NAV_WALK,46},{7,1,NAV_WALK,46},
    {3,8,NAV_WALK,60},{8,4,NAV_WALK,48},{8,5,NAV_WALK,48},
    {4,9,NAV_JET,38},{9,1,NAV_WALK,40},{3,10,NAV_JET,38},
    {10,7,NAV_WALK,42},{8,6,NAV_JET,40}
};

static const LayoutRoom nuclear_reactor_rooms[] = {
    {-445.5,-243,0,170,170,88,HALL},{445.5,243,0,170,170,88,HALL},
    {0,-432,48,180,150,0,COURTYARD},{405,-162,112,150,170,0,ROCK},
    {0,432,48,180,150,0,COURTYARD},{-405,162,112,150,170,0,ROCK},
    {0,0,88,160,160,0,ROCK},{-108,-189,0,120,100,0,PIT},
    {108,189,0,120,100,0,PIT},{-229.5,459,168,120,130,0,BRIDGE},
    {229.5,-459,168,120,130,0,BRIDGE}
};
static const LayoutLink nuclear_reactor_links[] = {
    {0,2,NAV_WALK,62},{2,3,NAV_WALK,48},{3,1,NAV_WALK,56},
    {1,4,NAV_WALK,62},{4,5,NAV_WALK,48},{5,0,NAV_WALK,56},
    {0,7,NAV_WALK,60},{7,8,NAV_WALK,56},{8,1,NAV_WALK,60},
    {2,6,NAV_WALK,46},{4,6,NAV_WALK,46},{3,6,NAV_JET,38},{5,6,NAV_JET,38},
    {7,6,NAV_JET,42},{8,6,NAV_JET,42},{5,9,NAV_WALK,42},{3,10,NAV_WALK,42},
    {9,6,NAV_JET,38},{10,6,NAV_JET,38}
};

#define PLAN(map,stem,alpha,bravo,neutral) {map,{stem##_rooms,sizeof(stem##_rooms)/sizeof(*stem##_rooms),stem##_links,sizeof(stem##_links)/sizeof(*stem##_links),alpha,bravo,neutral}}
static const struct { const char *name; Layout layout; } layouts[] = {
    PLAN("Arena",arena_ring,0,4,7), PLAN("Arena2",arena2_trench,0,1,5),
    PLAN("Arena3",arena3_bridge,0,1,4), PLAN("Aero",aero_mesa,0,1,6),
    PLAN("Airpirates",airpirates_lighthouse,0,1,5), PLAN("Lagrange",lagrange_woodland,0,1,6),
    PLAN("ctf_Ash",ash_fort,0,1,6), PLAN("ctf_Kampf",kampf_cavern,0,1,6),
    PLAN("ctf_Voland",voland_vault,1,0,7), PLAN("inf_Outpost",outpost_assault,0,1,8),
    PLAN("htf_Nuclear",nuclear_reactor,0,1,6)
};
#undef PLAN

const Layout *world_layout(const char *name) {
    for (size_t i=0;i<sizeof(layouts)/sizeof(*layouts);++i)
        if (!strcmp(name,layouts[i].name)) return &layouts[i].layout;
    const Layout *layout=world_layout_dm(name);
    if (layout) return layout;
    layout=world_layout_team(name);
    if (layout) return layout;
    fprintf(stderr,"No authored 3D layout for %s\n",name);
    abort();
}

const LayoutTerrain *world_terrain(const char *name)
{
    const LayoutTerrain *terrain=world_terrain_pilots(name);
    if(!terrain)terrain=world_terrain_dm(name);
    if(!terrain)terrain=world_terrain_team(name);
    if(terrain)return terrain;
    fprintf(stderr,"No authored terrain for %s\n",name);abort();
}
