#include "world_layouts.h"

#include <string.h>

static const LayoutRoom bigfalls_rooms[]={
    {-299,253,0,130,124,0,PIT},{0,299,0,130,100,0,COURTYARD},
    {299,253,16,130,124,0,PIT},{322,-23,48,116,110,0,ROCK},
    {299,-288,80,130,116,0,COURTYARD},{0,-299,104,220,140,0,BRIDGE},
    {-299,-288,72,124,110,0,ROCK},{-322,-23,32,110,116,0,ROCK},
    {0,0,128,110,100,0,ROCK}
};
static const LayoutLink bigfalls_links[]={
    {0,1,NAV_WALK,76},{1,2,NAV_WALK,72},{2,3,NAV_WALK,68},{3,4,NAV_WALK,68},
    {4,5,NAV_WALK,64},{5,6,NAV_WALK,64},{6,7,NAV_WALK,68},{7,0,NAV_WALK,72},
    {0,3,NAV_WALK,58},{2,7,NAV_WALK,58},{1,8,NAV_JET,48},{5,8,NAV_JET,48}
};

static const LayoutRoom blox_rooms[]={
    {-338,297,0,190,170,56,HALL},{0,351,0,116,104,0,COURTYARD},
    {338,297,32,170,190,64,HALL},{392,-27,64,116,110,0,ROCK},
    {297,-351,96,124,110,0,COURTYARD},{-54,-378,128,200,140,0,BRIDGE},
    {-378,-256,64,180,210,72,HALL},{-378,27,24,110,104,0,COURTYARD},
    {0,0,184,104,104,0,ROCK}
};
static const LayoutLink blox_links[]={
    {0,1,NAV_WALK,64},{1,2,NAV_WALK,60},{2,3,NAV_WALK,60},{3,4,NAV_WALK,60},
    {4,5,NAV_WALK,56},{5,6,NAV_WALK,60},{6,7,NAV_WALK,60},{7,0,NAV_WALK,60},
    {1,8,NAV_JET,48},{4,8,NAV_JET,48},{5,8,NAV_JET,48}
};

static const LayoutRoom bridge_rooms[]={
    {-352,0,32,200,210,64,HALL},{0,0,48,220,130,0,BRIDGE},{352,0,32,200,210,64,HALL},
    {-330,-264,0,116,116,0,COURTYARD},{0,-264,0,130,110,0,PIT},{330,-264,0,116,116,0,COURTYARD},
    {-330,264,0,116,116,0,COURTYARD},{0,264,0,130,110,0,PIT},{330,264,0,116,116,0,COURTYARD},
    {0,-517,88,104,100,0,ROCK}
};
static const LayoutLink bridge_links[]={
    {0,1,NAV_WALK,88},{1,2,NAV_WALK,88},{0,3,NAV_WALK,60},{3,4,NAV_WALK,72},
    {4,5,NAV_WALK,72},{5,2,NAV_WALK,60},{0,6,NAV_WALK,60},{6,7,NAV_WALK,64},
    {7,8,NAV_WALK,64},{8,2,NAV_WALK,60},{4,7,NAV_WALK,68},{4,9,NAV_JET,48}
};

static const LayoutRoom bunker_rooms[]={
    {0,300,0,130,116,0,COURTYARD},{-288,264,0,180,170,42,HALL},
    {-360,-12,0,150,210,38,HALL},{-288,-288,0,180,160,44,HALL},
    {0,-312,56,116,104,0,COURTYARD},{288,-288,0,180,160,44,HALL},
    {360,-12,0,150,210,38,HALL},{288,264,0,180,170,42,HALL},
    {0,0,0,200,200,48,HALL}
};
static const LayoutLink bunker_links[]={
    {0,1,NAV_WALK,52},{1,2,NAV_WALK,46},{2,3,NAV_WALK,46},{3,4,NAV_WALK,50},
    {4,5,NAV_WALK,50},{5,6,NAV_WALK,46},{6,7,NAV_WALK,46},{7,0,NAV_WALK,52},
    {1,8,NAV_WALK,46},{3,8,NAV_WALK,46},{5,8,NAV_WALK,46},{7,8,NAV_WALK,46},
    {0,4,NAV_JET,48}
};

static const LayoutRoom cambodia_rooms[]={
    {-375,188,0,130,116,0,COURTYARD},{-100,338,0,124,104,0,PIT},
    {225,288,16,124,116,0,ROCK},{400,0,40,116,130,0,COURTYARD},
    {200,-300,64,210,170,56,HALL},{-125,-325,64,124,116,0,COURTYARD},
    {-375,-112,24,180,160,50,HALL},{0,0,112,110,116,0,ROCK}
};
static const LayoutLink cambodia_links[]={
    {0,1,NAV_WALK,72},{1,2,NAV_WALK,60},{2,3,NAV_WALK,68},{3,4,NAV_WALK,64},
    {4,5,NAV_WALK,62},{5,6,NAV_WALK,58},{6,0,NAV_WALK,68},{1,5,NAV_WALK,52},
    {0,7,NAV_JET,48},{3,7,NAV_JET,48},{5,7,NAV_JET,48}
};

static const LayoutRoom crackedboot_rooms[]={
    {-375,250,0,130,116,0,PIT},{-88,338,16,116,104,0,COURTYARD},
    {225,275,32,130,104,0,ROCK},{388,0,0,150,210,48,HALL},
    {250,-288,48,116,110,0,COURTYARD},{-62,-338,64,124,110,0,ROCK},
    {-362,-200,32,124,116,0,COURTYARD},{-88,-38,128,100,116,0,ROCK},
    {100,25,0,130,160,40,HALL}
};
static const LayoutLink crackedboot_links[]={
    {0,1,NAV_WALK,70},{1,2,NAV_WALK,66},{2,3,NAV_WALK,58},{3,4,NAV_WALK,58},
    {4,5,NAV_WALK,66},{5,6,NAV_WALK,66},{6,0,NAV_WALK,72},
    {1,8,NAV_WALK,48},{8,4,NAV_WALK,48},{0,7,NAV_JET,44},{5,7,NAV_JET,44}
};

static const LayoutRoom daybreak_rooms[]={
    {-300,140,0,124,116,0,COURTYARD},{-60,240,24,180,160,48,HALL},
    {210,200,48,130,110,0,COURTYARD},{300,-50,48,116,130,0,ROCK},
    {100,-240,32,136,104,0,COURTYARD},{-170,-250,0,190,170,48,HALL},
    {-330,-70,0,100,116,0,ROCK},{-20,-10,88,200,130,0,BRIDGE}
};
static const LayoutLink daybreak_links[]={
    {0,1,NAV_WALK,66},{1,2,NAV_WALK,62},{2,3,NAV_WALK,70},{3,4,NAV_WALK,60},
    {4,5,NAV_WALK,64},{5,6,NAV_WALK,56},{6,0,NAV_WALK,66},
    {2,7,NAV_JET,48},{4,7,NAV_JET,48}
};

static const LayoutRoom desertwind_rooms[]={
    {-250,170,0,124,124,0,COURTYARD},{0,240,0,130,110,0,PIT},
    {250,150,0,124,124,0,COURTYARD},{250,-150,24,180,180,48,HALL},
    {0,-250,32,130,110,0,COURTYARD},{-250,-140,24,180,180,48,HALL},
    {-80,0,72,100,100,0,ROCK},{80,0,88,100,100,0,ROCK}
};
static const LayoutLink desertwind_links[]={
    {0,1,NAV_WALK,76},{1,2,NAV_WALK,76},{2,3,NAV_WALK,62},{3,4,NAV_WALK,62},
    {4,5,NAV_WALK,62},{5,0,NAV_WALK,62},{1,4,NAV_WALK,56},
    {0,6,NAV_JET,44},{4,6,NAV_JET,44},{2,7,NAV_JET,44},{4,7,NAV_JET,44}
};

static const LayoutRoom factory_rooms[]={
    {-320,0,0,200,200,52,HALL},{-280,-220,0,180,150,40,HALL},
    {-40,-260,0,200,150,44,HALL},{220,-220,24,190,170,44,HALL},
    {320,20,24,180,190,52,HALL},{220,250,24,190,160,40,HALL},
    {-30,260,0,200,150,44,HALL},{-280,220,0,180,160,40,HALL},
    {0,0,24,210,180,68,HALL}
};
static const LayoutLink factory_links[]={
    {0,1,NAV_WALK,48},{1,2,NAV_WALK,48},{2,3,NAV_WALK,48},{3,4,NAV_WALK,54},
    {4,5,NAV_WALK,48},{5,6,NAV_WALK,48},{6,7,NAV_WALK,48},{7,0,NAV_WALK,54},
    {0,8,NAV_WALK,64},{8,4,NAV_WALK,64},{2,8,NAV_WALK,44},{6,8,NAV_WALK,44}
};

static const LayoutRoom flashback_rooms[]={
    {-356,184,0,130,124,0,COURTYARD},{-92,310,0,116,104,0,PIT},
    {218,264,24,124,124,0,ROCK},{379,-23,48,116,130,0,COURTYARD},
    {195,-299,80,124,110,0,ROCK},{-103,-322,80,210,140,0,BRIDGE},
    {-356,-138,48,124,116,0,COURTYARD},{0,0,128,124,104,0,ROCK}
};
static const LayoutLink flashback_links[]={
    {0,1,NAV_WALK,76},{1,2,NAV_WALK,64},{2,3,NAV_WALK,62},{3,4,NAV_WALK,60},
    {4,5,NAV_WALK,56},{5,6,NAV_WALK,60},{6,0,NAV_WALK,68},{0,3,NAV_WALK,52},
    {1,7,NAV_JET,48},{4,7,NAV_JET,48},{6,7,NAV_JET,48}
};

static const LayoutRoom hh_rooms[]={
    {-438,200,0,190,190,52,HALL},{-425,-162,0,124,116,0,COURTYARD},
    {-112,-300,40,210,150,0,BRIDGE},{225,-250,72,124,116,0,ROCK},
    {450,0,48,170,210,56,HALL},{275,288,0,130,116,0,COURTYARD},
    {-50,275,0,130,104,0,PIT},{0,-12,104,190,130,0,BRIDGE}
};
static const LayoutLink hh_links[]={
    {0,1,NAV_WALK,64},{1,2,NAV_WALK,58},{2,3,NAV_WALK,58},{3,4,NAV_WALK,60},
    {4,5,NAV_WALK,64},{5,6,NAV_WALK,72},{6,0,NAV_WALK,72},{1,6,NAV_WALK,54},
    {2,7,NAV_WALK,56},{5,7,NAV_JET,48},{1,7,NAV_JET,48}
};

static const LayoutRoom island_rooms[]={
    {0,362,0,130,110,0,PIT},{-338,250,0,136,110,0,COURTYARD},
    {-425,-75,24,116,130,0,ROCK},{-188,-338,56,124,110,0,COURTYARD},
    {175,-350,72,124,116,0,ROCK},{412,-75,24,116,130,0,COURTYARD},
    {325,250,0,130,116,0,PIT},{-38,-12,128,124,116,0,ROCK}
};
static const LayoutLink island_links[]={
    {0,1,NAV_WALK,80},{1,2,NAV_WALK,70},{2,3,NAV_WALK,62},{3,4,NAV_WALK,58},
    {4,5,NAV_WALK,62},{5,6,NAV_WALK,70},{6,0,NAV_WALK,80},{1,5,NAV_WALK,56},
    {0,7,NAV_JET,48},{3,7,NAV_JET,48},{4,7,NAV_JET,48}
};

static const LayoutRoom jungle_rooms[]={
    {-336,228,0,124,116,0,PIT},{-48,336,0,138,122,0,COURTYARD},
    {276,228,24,130,110,0,ROCK},{360,-84,32,130,138,0,COURTYARD},
    {96,-324,56,150,122,0,ROCK},{-240,-276,40,190,180,44,HALL},
    {-396,-48,16,100,116,0,COURTYARD},{-60,24,112,110,110,0,ROCK}
};
static const LayoutLink jungle_links[]={
    {0,1,NAV_WALK,74},{1,2,NAV_WALK,62},{2,3,NAV_WALK,64},{3,4,NAV_WALK,62},
    {4,5,NAV_WALK,58},{5,6,NAV_WALK,50},{6,0,NAV_WALK,62},{1,4,NAV_WALK,48},
    {0,7,NAV_JET,44},{2,7,NAV_JET,44},{4,7,NAV_JET,44}
};

static const LayoutRoom krab_rooms[]={
    {0,0,32,136,130,0,COURTYARD},{-275,209,0,116,116,0,PIT},
    {275,209,0,116,116,0,PIT},{-385,-22,24,160,190,48,HALL},
    {385,-22,24,160,190,48,HALL},{-275,-286,64,110,116,0,ROCK},
    {275,-286,64,110,116,0,ROCK},{0,-330,96,210,130,0,BRIDGE},
    {0,297,0,124,100,0,COURTYARD}
};
static const LayoutLink krab_links[]={
    {8,1,NAV_WALK,72},{8,2,NAV_WALK,72},{1,3,NAV_WALK,60},{2,4,NAV_WALK,60},
    {3,5,NAV_WALK,54},{4,6,NAV_WALK,54},{5,7,NAV_WALK,58},{6,7,NAV_WALK,58},
    {1,0,NAV_WALK,56},{2,0,NAV_WALK,56},{0,7,NAV_JET,48},{0,5,NAV_JET,44},{0,6,NAV_JET,44}
};

static const LayoutRoom leaf_rooms[]={
    {-275,187,0,116,110,0,COURTYARD},{0,264,16,130,104,0,ROCK},
    {275,121,0,116,116,0,COURTYARD},{242,-187,40,116,110,0,ROCK},
    {-33,-275,64,124,104,0,COURTYARD},{-286,-99,24,110,116,0,ROCK},
    {0,0,112,104,100,0,ROCK}
};
static const LayoutLink leaf_links[]={
    {0,1,NAV_WALK,70},{1,2,NAV_WALK,64},{2,3,NAV_WALK,62},{3,4,NAV_WALK,58},
    {4,5,NAV_WALK,58},{5,0,NAV_WALK,62},{0,3,NAV_WALK,50},
    {1,6,NAV_JET,44},{4,6,NAV_JET,44}
};

static const LayoutRoom snowman_rooms[]={
    {-304,220,0,130,116,0,COURTYARD},{0,294,0,130,110,0,PIT},
    {304,220,0,130,116,0,COURTYARD},{315,-42,24,170,190,52,HALL},
    {178,-284,48,130,110,0,COURTYARD},{-105,-294,48,210,150,0,BRIDGE},
    {-315,-84,24,180,190,52,HALL},{-32,0,88,124,104,0,ROCK}
};
static const LayoutLink snowman_links[]={
    {0,1,NAV_WALK,80},{1,2,NAV_WALK,80},{2,3,NAV_WALK,60},{3,4,NAV_WALK,60},
    {4,5,NAV_WALK,58},{5,6,NAV_WALK,60},{6,0,NAV_WALK,60},{0,4,NAV_WALK,54},
    {1,7,NAV_JET,48},{5,7,NAV_JET,48}
};

static const LayoutRoom rr_rooms[]={
    {-260,110,0,170,170,40,HALL},{-250,-130,0,110,110,0,COURTYARD},
    {-20,-230,24,190,150,44,HALL},{230,-140,24,116,110,0,COURTYARD},
    {270,110,0,170,170,40,HALL},{20,230,0,124,104,0,COURTYARD},
    {0,0,56,100,100,0,ROCK}
};
static const LayoutLink rr_links[]={
    {0,1,NAV_WALK,54},{1,2,NAV_WALK,52},{2,3,NAV_WALK,52},{3,4,NAV_WALK,54},
    {4,5,NAV_WALK,60},{5,0,NAV_WALK,60},{1,3,NAV_WALK,48},
    {5,6,NAV_JET,44},{3,6,NAV_JET,44}
};

static const LayoutRoom ratcave_rooms[]={
    {-351,297,0,190,180,48,HALL},{0,364,0,136,110,0,PIT},
    {351,297,24,180,170,44,HALL},{418,-27,56,110,130,0,ROCK},
    {243,-338,88,190,180,64,HALL},{-122,-364,112,136,104,0,COURTYARD},
    {-392,-122,56,180,190,48,HALL},{0,0,176,110,104,0,ROCK},
    {-675,94,16,150,190,36,HALL}
};
static const LayoutLink ratcave_links[]={
    {0,1,NAV_WALK,56},{1,2,NAV_WALK,56},{2,3,NAV_WALK,48},{3,4,NAV_WALK,50},
    {4,5,NAV_WALK,52},{5,6,NAV_WALK,50},{6,8,NAV_WALK,42},{8,0,NAV_WALK,42},
    {6,0,NAV_WALK,52},{1,7,NAV_JET,44},{3,7,NAV_JET,44},{5,7,NAV_JET,44}
};

static const LayoutRoom rok_rooms[]={
    {-322,253,0,124,110,0,PIT},{0,345,0,130,104,0,COURTYARD},
    {322,241,40,116,116,0,ROCK},{356,-80,80,124,116,0,COURTYARD},
    {92,-356,120,210,140,0,BRIDGE},{-241,-288,80,116,124,0,ROCK},
    {-391,-23,40,110,116,0,COURTYARD},{-23,0,216,116,116,0,ROCK},
    {368,-391,192,100,100,0,ROCK}
};
static const LayoutLink rok_links[]={
    {0,1,NAV_WALK,70},{1,2,NAV_WALK,64},{2,3,NAV_WALK,60},{3,4,NAV_WALK,60},
    {4,5,NAV_WALK,56},{5,6,NAV_WALK,60},{6,0,NAV_WALK,64},{1,3,NAV_WALK,52},
    {1,7,NAV_JET,48},{4,7,NAV_JET,48},{6,7,NAV_JET,48},{3,8,NAV_JET,44},{7,8,NAV_JET,44}
};

static const LayoutRoom shau_rooms[]={
    {-492,96,24,124,124,0,COURTYARD},{-264,300,0,124,110,0,PIT},
    {48,336,0,136,104,0,COURTYARD},{372,216,16,124,124,0,PIT},
    {504,-72,48,104,124,0,ROCK},{192,-264,56,136,110,0,COURTYARD},
    {-132,-300,64,210,140,0,BRIDGE},{-432,-192,48,132,112,0,ROCK},
    {-96,24,24,200,180,44,HALL}
};
static const LayoutLink shau_links[]={
    {0,1,NAV_WALK,76},{1,2,NAV_WALK,76},{2,3,NAV_WALK,76},{3,4,NAV_WALK,66},
    {4,5,NAV_WALK,64},{5,6,NAV_WALK,62},{6,7,NAV_WALK,62},{7,0,NAV_WALK,66},
    {1,8,NAV_WALK,52},{8,5,NAV_WALK,52},{0,8,NAV_WALK,50},{2,6,NAV_JET,48}
};

static const LayoutRoom tropiccave_rooms[]={
    {-320,0,0,180,190,48,HALL},{-200,220,0,116,110,0,COURTYARD},
    {50,250,16,190,160,44,HALL},{290,160,24,116,110,0,COURTYARD},
    {320,-100,24,170,180,48,HALL},{80,-250,40,130,110,0,COURTYARD},
    {-190,-230,24,180,170,44,HALL},{0,0,80,110,100,0,ROCK}
};
static const LayoutLink tropiccave_links[]={
    {0,1,NAV_WALK,56},{1,2,NAV_WALK,54},{2,3,NAV_WALK,54},{3,4,NAV_WALK,56},
    {4,5,NAV_WALK,54},{5,6,NAV_WALK,54},{6,0,NAV_WALK,56},{1,5,NAV_WALK,44},
    {1,7,NAV_JET,44},{3,7,NAV_JET,44},{5,7,NAV_JET,44}
};

static const LayoutRoom unlim_rooms[]={
    {-345,184,0,124,116,0,COURTYARD},{-69,288,24,130,104,0,ROCK},
    {264,207,48,124,116,0,COURTYARD},{345,-92,24,170,190,52,HALL},
    {92,-299,0,130,110,0,PIT},{-218,-253,24,190,170,52,HALL},
    {-46,0,112,180,150,0,BRIDGE},{-471,-103,56,100,110,0,ROCK}
};
static const LayoutLink unlim_links[]={
    {0,1,NAV_WALK,70},{1,2,NAV_WALK,64},{2,3,NAV_WALK,60},{3,4,NAV_WALK,60},
    {4,5,NAV_WALK,60},{5,7,NAV_WALK,54},{7,0,NAV_WALK,56},{0,5,NAV_WALK,54},
    {1,6,NAV_JET,48},{2,6,NAV_JET,48},{4,6,NAV_JET,48}
};

static const LayoutRoom veoto_rooms[]={
    {-330,253,0,130,110,0,COURTYARD},{-44,319,0,130,104,0,PIT},
    {275,231,0,130,116,0,COURTYARD},{374,-22,32,170,190,48,HALL},
    {209,-297,64,124,116,0,ROCK},{-110,-319,64,210,160,56,HALL},
    {-363,-99,32,116,124,0,COURTYARD},{-44,0,112,130,110,0,COURTYARD}
};
static const LayoutLink veoto_links[]={
    {0,1,NAV_WALK,74},{1,2,NAV_WALK,74},{2,3,NAV_WALK,60},{3,4,NAV_WALK,58},
    {4,5,NAV_WALK,62},{5,6,NAV_WALK,58},{6,0,NAV_WALK,64},{0,3,NAV_WALK,50},
    {1,7,NAV_JET,48},{4,7,NAV_JET,48},{6,7,NAV_JET,48}
};

#define PLAN(name,prefix,alpha,bravo,neutral) {name,{prefix##_rooms,sizeof(prefix##_rooms)/sizeof(*prefix##_rooms),prefix##_links,sizeof(prefix##_links)/sizeof(*prefix##_links),alpha,bravo,neutral}}
static const struct {const char *name;Layout layout;} layouts[]={
    PLAN("Bigfalls",bigfalls,0,4,8),PLAN("Blox",blox,0,4,8),PLAN("Bridge",bridge,0,2,1),
    PLAN("Bunker",bunker,1,5,8),PLAN("Cambodia",cambodia,0,3,7),PLAN("CrackedBoot",crackedboot,0,4,7),
    PLAN("Daybreak",daybreak,0,3,7),PLAN("DesertWind",desertwind,0,2,4),PLAN("Factory",factory,0,4,8),
    PLAN("Flashback",flashback,0,3,7),PLAN("HH",hh,0,4,7),PLAN("Island2k5",island,1,5,7),
    PLAN("Jungle",jungle,0,3,7),PLAN("Krab",krab,1,2,0),PLAN("Leaf",leaf,0,2,6),
    PLAN("MrSnowman",snowman,0,2,7),PLAN("RR",rr,0,4,6),PLAN("RatCave",ratcave,0,4,7),
    PLAN("Rok",rok,0,3,7),PLAN("Shau",shau,0,4,8),PLAN("Tropiccave",tropiccave,0,4,7),
    PLAN("Unlim",unlim,0,3,6),PLAN("Veoto",veoto,0,3,7)
};
#undef PLAN

const Layout *world_layout_dm(const char *name) {
    for(size_t i=0;i<sizeof(layouts)/sizeof(*layouts);++i)
        if(!strcmp(name,layouts[i].name))return &layouts[i].layout;
    return NULL;
}
