#include "world_layouts.h"
#include <string.h>

#define ROOMS(...) ((const LayoutRoom[]){__VA_ARGS__})
#define LINKS(...) ((const LayoutLink[]){__VA_ARGS__})
#define PLAN(name, alpha, bravo, neutral, rooms, links) \
    {name, {rooms, sizeof(rooms) / sizeof(LayoutRoom), links, sizeof(links) / sizeof(LayoutLink), alpha, bravo, neutral}}

static const struct { const char *name; Layout layout; } layouts[] = {
    PLAN("ctf_Aftermath", 0, 1, 2, ROOMS(
        {-609, 0, 24, 170, 160, 96, HALL},
        {609, 0, 24, 170, 160, 96, HALL},
        {0, 0, 96, 160, 170, 0, ROCK},
        {-319, -261, 48, 180, 150, 84, HALL},
        {0, -355, 32, 180, 160, 0, COURTYARD},
        {319, -261, 48, 180, 150, 84, HALL},
        {-319, 261, 0, 170, 170, 0, PIT},
        {0, 341, 12, 200, 150, 76, HALL},
        {319, 261, 0, 170, 170, 0, PIT}
    ), LINKS(
        {0, 3, NAV_WALK, 68}, {3, 4, NAV_WALK, 74}, {4, 5, NAV_WALK, 74},
        {5, 1, NAV_WALK, 68}, {0, 6, NAV_WALK, 76}, {6, 7, NAV_WALK, 70},
        {7, 8, NAV_WALK, 70}, {8, 1, NAV_WALK, 76}, {3, 2, NAV_WALK, 62},
        {5, 2, NAV_WALK, 62}, {7, 2, NAV_WALK, 66}
    )),
    PLAN("ctf_Amnesia", 0, 1, 2, ROOMS(
        {-702, 0, 36, 180, 170, 80, HALL},
        {702, 0, 36, 180, 170, 80, HALL},
        {0, -144, 112, 160, 140, 0, BRIDGE},
        {-342, 0, 20, 170, 150, 72, HALL},
        {342, 0, 20, 170, 150, 72, HALL},
        {-342, 360, 0, 160, 160, 64, HALL},
        {0, 450, 0, 180, 160, 64, HALL},
        {342, 360, 0, 160, 160, 64, HALL},
        {0, -486, 48, 190, 150, 0, COURTYARD}
    ), LINKS(
        {0, 3, NAV_WALK, 74}, {3, 4, NAV_WALK, 66}, {4, 1, NAV_WALK, 74},
        {3, 5, NAV_WALK, 62}, {5, 6, NAV_WALK, 62}, {6, 7, NAV_WALK, 62},
        {7, 4, NAV_WALK, 62}, {0, 8, NAV_WALK, 80}, {8, 1, NAV_WALK, 80},
        {8, 2, NAV_WALK, 68}, {3, 2, NAV_WALK, 58}, {4, 2, NAV_WALK, 58}
    )),
    PLAN("ctf_B2b", 0, 1, 2, ROOMS(
        {-516, -144, 72, 160, 180, 0, ROCK},
        {516, 144, 72, 160, 180, 0, ROCK},
        {0, 0, 0, 200, 170, 0, PIT},
        {-282, -84, 38, 160, 150, 0, ROCK},
        {282, 84, 38, 160, 150, 0, ROCK},
        {-264, 204, 12, 180, 160, 0, COURTYARD},
        {24, 288, 26, 180, 160, 0, BRIDGE},
        {264, -204, 12, 180, 160, 0, COURTYARD},
        {-24, -288, 26, 180, 160, 0, BRIDGE}
    ), LINKS(
        {0, 3, NAV_WALK, 86}, {3, 2, NAV_WALK, 90}, {2, 4, NAV_WALK, 90},
        {4, 1, NAV_WALK, 86}, {0, 5, NAV_WALK, 76}, {5, 6, NAV_WALK, 76},
        {6, 1, NAV_WALK, 76}, {1, 7, NAV_WALK, 76}, {7, 8, NAV_WALK, 76},
        {8, 0, NAV_WALK, 76}, {5, 2, NAV_WALK, 60}, {7, 2, NAV_WALK, 60}
    )),
    PLAN("ctf_Blade", 0, 1, 2, ROOMS(
        {-520, 0, 48, 160, 170, 80, HALL},
        {520, 0, 48, 160, 170, 80, HALL},
        {0, -78, 112, 160, 180, 0, ROCK},
        {-286, -234, 60, 170, 180, 0, COURTYARD},
        {286, -234, 60, 170, 180, 0, COURTYARD},
        {-299, 221, 12, 160, 180, 0, COURTYARD},
        {299, 221, 12, 160, 180, 0, COURTYARD},
        {0, 306, 0, 220, 140, 0, COURTYARD},
        {0, -370, 44, 190, 160, 0, BRIDGE}
    ), LINKS(
        {0, 3, NAV_WALK, 70}, {3, 8, NAV_WALK, 64}, {8, 4, NAV_WALK, 64},
        {4, 1, NAV_WALK, 70}, {0, 5, NAV_WALK, 70}, {5, 7, NAV_WALK, 86},
        {7, 6, NAV_WALK, 86}, {6, 1, NAV_WALK, 70}, {3, 2, NAV_JET, 58},
        {4, 2, NAV_JET, 58}, {2, 7, NAV_JET, 72}
    )),
    PLAN("ctf_Campeche", 0, 1, 2, ROOMS(
        {-555, 180, 24, 180, 160, 84, HALL},
        {555, 180, 24, 180, 160, 84, HALL},
        {0, -195, 128, 190, 200, 0, ROCK},
        {-308, -105, 64, 160, 170, 0, COURTYARD},
        {308, -105, 64, 160, 170, 0, COURTYARD},
        {0, 225, 44, 190, 160, 92, HALL},
        {-240, 480, 0, 160, 150, 0, PIT},
        {240, 480, 0, 160, 150, 0, PIT},
        {0, -532, 196, 160, 140, 0, BRIDGE}
    ), LINKS(
        {0, 3, NAV_WALK, 76}, {3, 2, NAV_WALK, 66}, {2, 4, NAV_WALK, 66},
        {4, 1, NAV_WALK, 76}, {0, 5, NAV_WALK, 86}, {5, 1, NAV_WALK, 86},
        {0, 6, NAV_WALK, 66}, {6, 7, NAV_WALK, 76}, {7, 1, NAV_WALK, 66},
        {5, 2, NAV_WALK, 72}, {2, 8, NAV_JET, 62}, {6, 5, NAV_WALK, 72},
        {7, 5, NAV_WALK, 72}
    )),
    PLAN("ctf_Cobra", 0, 1, 2, ROOMS(
        {-532, -252, 112, 160, 170, 0, ROCK},
        {532, -252, 112, 160, 170, 0, ROCK},
        {0, 238, 0, 210, 170, 0, PIT},
        {-259, -35, 52, 180, 150, 0, BRIDGE},
        {259, -35, 52, 180, 150, 0, BRIDGE},
        {-420, 238, 32, 180, 170, 0, COURTYARD},
        {420, 238, 32, 180, 170, 0, COURTYARD},
        {0, -252, 100, 190, 140, 0, ROCK}
    ), LINKS(
        {0, 3, NAV_WALK, 74}, {3, 2, NAV_WALK, 86}, {2, 4, NAV_WALK, 86},
        {4, 1, NAV_WALK, 74}, {0, 5, NAV_WALK, 80}, {5, 2, NAV_WALK, 82},
        {2, 6, NAV_WALK, 82}, {6, 1, NAV_WALK, 80}, {3, 7, NAV_JET, 66},
        {7, 4, NAV_JET, 66}, {0, 7, NAV_WALK, 60}, {7, 1, NAV_WALK, 60}
    )),
    PLAN("ctf_Crucifix", 0, 1, 2, ROOMS(
        {-533, 0, 48, 180, 170, 88, HALL},
        {533, 0, 48, 180, 170, 88, HALL},
        {0, 0, 64, 200, 200, 0, COURTYARD},
        {-273, 0, 48, 150, 160, 80, HALL},
        {273, 0, 48, 150, 160, 80, HALL},
        {0, -325, 164, 180, 180, 0, BRIDGE},
        {0, 325, 0, 180, 180, 0, COURTYARD},
        {-299, 299, 12, 160, 160, 0, ROCK},
        {299, -299, 112, 160, 160, 0, ROCK}
    ), LINKS(
        {0, 3, NAV_WALK, 86}, {3, 2, NAV_WALK, 76}, {2, 4, NAV_WALK, 76},
        {4, 1, NAV_WALK, 86}, {2, 5, NAV_JET, 76}, {2, 6, NAV_WALK, 86},
        {0, 7, NAV_WALK, 70}, {7, 6, NAV_WALK, 70}, {6, 1, NAV_WALK, 70},
        {1, 8, NAV_WALK, 70}, {8, 5, NAV_WALK, 70}, {5, 0, NAV_WALK, 70}
    )),
    PLAN("ctf_Death", 0, 1, 2, ROOMS(
        {-493, -203, 80, 160, 170, 84, HALL},
        {493, -203, 80, 160, 170, 84, HALL},
        {0, -58, 112, 160, 170, 0, ROCK},
        {-276, 130, 48, 160, 160, 88, HALL},
        {276, 130, 48, 160, 160, 88, HALL},
        {0, 377, 0, 210, 170, 0, PIT},
        {0, -413, 136, 200, 160, 0, BRIDGE},
        {-522, 290, 24, 150, 170, 0, ROCK},
        {522, 290, 24, 150, 170, 0, ROCK}
    ), LINKS(
        {0, 3, NAV_WALK, 74}, {3, 5, NAV_WALK, 78}, {5, 4, NAV_WALK, 78},
        {4, 1, NAV_WALK, 74}, {0, 6, NAV_WALK, 68}, {6, 1, NAV_WALK, 68},
        {0, 7, NAV_WALK, 64}, {7, 5, NAV_WALK, 64}, {1, 8, NAV_WALK, 64},
        {8, 5, NAV_WALK, 64}, {3, 2, NAV_WALK, 66}, {4, 2, NAV_WALK, 66},
        {2, 6, NAV_WALK, 62}
    )),
    PLAN("ctf_Division", 0, 1, 2, ROOMS(
        {-518, 23, 32, 170, 190, 72, HALL},
        {518, -23, 32, 170, 190, 72, HALL},
        {0, 0, 76, 170, 180, 0, ROCK},
        {-264, -184, 48, 180, 150, 88, HALL},
        {241, -241, 20, 190, 150, 0, COURTYARD},
        {264, 184, 48, 180, 150, 88, HALL},
        {-241, 241, 20, 190, 150, 0, COURTYARD},
        {0, -368, 0, 180, 140, 80, HALL},
        {0, 368, 0, 180, 140, 80, HALL}
    ), LINKS(
        {0, 3, NAV_WALK, 68}, {3, 7, NAV_WALK, 68}, {7, 4, NAV_WALK, 68},
        {4, 1, NAV_WALK, 76}, {1, 5, NAV_WALK, 68}, {5, 8, NAV_WALK, 68},
        {8, 6, NAV_WALK, 68}, {6, 0, NAV_WALK, 76}, {3, 2, NAV_WALK, 70},
        {2, 5, NAV_WALK, 70}, {4, 2, NAV_JET, 60}, {6, 2, NAV_JET, 60}
    )),
    PLAN("ctf_Dropdown", 0, 1, 2, ROOMS(
        {-588, 0, 84, 170, 170, 0, ROCK},
        {588, 0, 84, 170, 170, 0, ROCK},
        {0, 0, 0, 220, 180, 0, PIT},
        {-294, -210, 44, 170, 180, 0, COURTYARD},
        {294, -210, 44, 170, 180, 0, COURTYARD},
        {-294, 238, 28, 170, 170, 92, HALL},
        {294, 238, 28, 170, 170, 92, HALL},
        {0, -392, 108, 180, 160, 0, BRIDGE},
        {0, 392, 0, 180, 160, 72, HALL}
    ), LINKS(
        {0, 3, NAV_WALK, 82}, {3, 2, NAV_WALK, 82}, {2, 4, NAV_WALK, 82},
        {4, 1, NAV_WALK, 82}, {0, 5, NAV_WALK, 72}, {5, 8, NAV_WALK, 72},
        {8, 6, NAV_WALK, 72}, {6, 1, NAV_WALK, 72}, {3, 7, NAV_WALK, 68},
        {7, 4, NAV_WALK, 68}, {2, 7, NAV_JET, 70}, {2, 8, NAV_WALK, 76}
    )),
    PLAN("ctf_Equinox", 0, 1, 2, ROOMS(
        {-559, -52, 28, 180, 180, 0, ROCK},
        {559, 52, 28, 180, 180, 0, ROCK},
        {0, 0, 54, 180, 160, 0, BRIDGE},
        {-280, -182, 12, 180, 160, 0, COURTYARD},
        {280, 182, 12, 180, 160, 0, COURTYARD},
        {-273, 208, 0, 170, 160, 80, HALL},
        {273, -208, 0, 170, 160, 80, HALL},
        {0, -351, 20, 180, 150, 0, ROCK},
        {0, 351, 20, 180, 150, 0, ROCK}
    ), LINKS(
        {0, 3, NAV_WALK, 82}, {3, 7, NAV_WALK, 72}, {7, 6, NAV_WALK, 72},
        {6, 1, NAV_WALK, 82}, {0, 5, NAV_WALK, 82}, {5, 8, NAV_WALK, 72},
        {8, 4, NAV_WALK, 72}, {4, 1, NAV_WALK, 82}, {3, 2, NAV_WALK, 72},
        {2, 4, NAV_WALK, 72}, {5, 2, NAV_WALK, 70}, {6, 2, NAV_WALK, 70}
    )),
    PLAN("ctf_Guardian", 0, 1, 2, ROOMS(
        {-551, -145, 112, 170, 180, 0, ROCK},
        {551, -145, 112, 170, 180, 0, ROCK},
        {0, 130, 28, 200, 180, 0, COURTYARD},
        {-304, 145, 64, 170, 160, 0, ROCK},
        {304, 145, 64, 170, 160, 0, ROCK},
        {0, -246, 144, 180, 160, 0, BRIDGE},
        {-334, 435, 0, 160, 150, 76, HALL},
        {0, 493, 0, 180, 150, 88, HALL},
        {334, 435, 0, 160, 150, 76, HALL}
    ), LINKS(
        {0, 3, NAV_WALK, 82}, {3, 2, NAV_WALK, 82}, {2, 4, NAV_WALK, 82},
        {4, 1, NAV_WALK, 82}, {0, 5, NAV_WALK, 72}, {5, 1, NAV_WALK, 72},
        {3, 6, NAV_WALK, 76}, {6, 7, NAV_WALK, 76}, {7, 8, NAV_WALK, 76},
        {8, 4, NAV_WALK, 76}, {2, 5, NAV_JET, 76}, {2, 7, NAV_WALK, 82}
    )),
    PLAN("ctf_Hormone", 0, 1, 2, ROOMS(
        {-448, 0, 28, 180, 170, 0, COURTYARD},
        {448, 0, 28, 180, 170, 0, COURTYARD},
        {0, 0, 0, 180, 190, 0, PIT},
        {-224, -178, 24, 170, 160, 72, HALL},
        {224, -178, 24, 170, 160, 72, HALL},
        {-224, 178, 24, 170, 160, 72, HALL},
        {224, 178, 24, 170, 160, 72, HALL},
        {0, -322, 48, 180, 160, 0, BRIDGE},
        {0, 322, 48, 180, 160, 0, BRIDGE}
    ), LINKS(
        {0, 3, NAV_WALK, 76}, {3, 7, NAV_WALK, 76}, {7, 4, NAV_WALK, 76},
        {4, 1, NAV_WALK, 76}, {0, 5, NAV_WALK, 76}, {5, 8, NAV_WALK, 76},
        {8, 6, NAV_WALK, 76}, {6, 1, NAV_WALK, 76}, {3, 2, NAV_WALK, 72},
        {2, 6, NAV_WALK, 72}, {5, 2, NAV_WALK, 72}, {2, 4, NAV_WALK, 72}
    )),
    PLAN("ctf_IceBeam", 0, 1, 2, ROOMS(
        {-555, 0, 44, 170, 170, 96, HALL},
        {555, 0, 44, 170, 170, 96, HALL},
        {0, -105, 144, 220, 140, 0, BRIDGE},
        {-330, -345, 96, 160, 180, 0, ROCK},
        {330, -345, 96, 160, 180, 0, ROCK},
        {0, 300, 24, 220, 160, 104, HALL},
        {-330, 345, 0, 160, 180, 0, COURTYARD},
        {330, 345, 0, 160, 180, 0, COURTYARD},
        {0, -540, 176, 180, 160, 0, ROCK}
    ), LINKS(
        {0, 3, NAV_WALK, 80}, {3, 8, NAV_WALK, 72}, {8, 4, NAV_WALK, 72},
        {4, 1, NAV_WALK, 80}, {0, 6, NAV_WALK, 84}, {6, 5, NAV_WALK, 84},
        {5, 7, NAV_WALK, 84}, {7, 1, NAV_WALK, 84}, {3, 2, NAV_WALK, 66},
        {2, 4, NAV_WALK, 66}, {5, 2, NAV_WALK, 74}
    )),
    PLAN("ctf_Lanubya", 0, 1, 2, ROOMS(
        {-550, 0, 44, 180, 180, 84, HALL},
        {550, 0, 44, 180, 180, 84, HALL},
        {0, 0, 0, 220, 180, 0, PIT},
        {-275, -225, 64, 170, 180, 0, ROCK},
        {275, -225, 64, 170, 180, 0, ROCK},
        {0, -325, 108, 200, 150, 0, BRIDGE},
        {-275, 225, 32, 170, 180, 0, COURTYARD},
        {275, 225, 32, 170, 180, 0, COURTYARD},
        {0, 325, 56, 200, 150, 76, HALL}
    ), LINKS(
        {0, 3, NAV_WALK, 76}, {3, 5, NAV_WALK, 70}, {5, 4, NAV_WALK, 70},
        {4, 1, NAV_WALK, 76}, {0, 6, NAV_WALK, 82}, {6, 8, NAV_WALK, 80},
        {8, 7, NAV_WALK, 80}, {7, 1, NAV_WALK, 82}, {6, 2, NAV_WALK, 80},
        {2, 7, NAV_WALK, 80}, {2, 5, NAV_JET, 70}
    )),
    PLAN("ctf_Laos", 0, 1, 2, ROOMS(
        {-758, -148, 40, 180, 160, 0, ROCK},
        {758, 148, 40, 180, 160, 0, ROCK},
        {0, 0, 88, 220, 190, 0, ROCK},
        {-370, -388, 76, 170, 170, 0, COURTYARD},
        {370, 388, 76, 170, 170, 0, COURTYARD},
        {-352, 259, 0, 170, 160, 88, HALL},
        {352, -259, 0, 170, 160, 88, HALL},
        {0, -610, 108, 190, 150, 0, BRIDGE},
        {0, 610, 108, 190, 150, 0, BRIDGE}
    ), LINKS(
        {0, 3, NAV_WALK, 72}, {3, 7, NAV_WALK, 72}, {7, 6, NAV_WALK, 76},
        {6, 1, NAV_WALK, 76}, {0, 5, NAV_WALK, 76}, {5, 8, NAV_WALK, 76},
        {8, 4, NAV_WALK, 72}, {4, 1, NAV_WALK, 72}, {3, 2, NAV_WALK, 70},
        {2, 4, NAV_WALK, 70}, {5, 2, NAV_WALK, 64}, {6, 2, NAV_WALK, 64}
    )),
    PLAN("ctf_MFM", 0, 1, 2, ROOMS(
        {-526, 0, 32, 170, 170, 80, HALL},
        {526, 0, 32, 170, 170, 80, HALL},
        {0, 0, 64, 180, 160, 80, HALL},
        {-256, -256, 72, 170, 160, 92, HALL},
        {256, -256, 72, 170, 160, 92, HALL},
        {-256, 256, 0, 170, 160, 72, HALL},
        {256, 256, 0, 170, 160, 72, HALL},
        {0, -405, 112, 180, 160, 0, BRIDGE},
        {0, 405, 0, 180, 160, 0, PIT}
    ), LINKS(
        {0, 3, NAV_WALK, 76}, {3, 7, NAV_WALK, 66}, {7, 4, NAV_WALK, 66},
        {4, 1, NAV_WALK, 76}, {0, 5, NAV_WALK, 76}, {5, 8, NAV_WALK, 66},
        {8, 6, NAV_WALK, 66}, {6, 1, NAV_WALK, 76}, {3, 2, NAV_WALK, 66},
        {2, 4, NAV_WALK, 66}, {5, 2, NAV_WALK, 76}, {2, 6, NAV_WALK, 76}
    )),
    PLAN("ctf_Maya", 0, 1, 2, ROOMS(
        {-630, 120, 12, 180, 170, 80, HALL},
        {630, 120, 12, 180, 170, 80, HALL},
        {0, -210, 136, 180, 180, 0, ROCK},
        {-330, -150, 76, 170, 170, 0, ROCK},
        {330, -150, 76, 170, 170, 0, ROCK},
        {0, 135, 56, 200, 160, 96, HALL},
        {-330, 345, 0, 170, 160, 0, COURTYARD},
        {330, 345, 0, 170, 160, 0, COURTYARD},
        {0, 495, 20, 200, 150, 72, HALL}
    ), LINKS(
        {0, 3, NAV_WALK, 76}, {3, 2, NAV_WALK, 68}, {2, 4, NAV_WALK, 68},
        {4, 1, NAV_WALK, 76}, {0, 6, NAV_WALK, 82}, {6, 8, NAV_WALK, 82},
        {8, 7, NAV_WALK, 82}, {7, 1, NAV_WALK, 82}, {3, 5, NAV_WALK, 72},
        {5, 4, NAV_WALK, 72}, {5, 2, NAV_WALK, 70}, {5, 8, NAV_WALK, 76}
    )),
    PLAN("ctf_Mayapan", 0, 1, 2, ROOMS(
        {-540, 0, 28, 180, 170, 92, HALL},
        {540, 0, 28, 180, 170, 92, HALL},
        {0, 0, 60, 180, 170, 0, COURTYARD},
        {-276, -192, 56, 170, 160, 80, HALL},
        {276, -192, 56, 170, 160, 80, HALL},
        {-276, 204, 0, 170, 160, 72, HALL},
        {276, 204, 0, 170, 160, 72, HALL},
        {0, -342, 84, 200, 150, 0, BRIDGE},
        {0, 342, 0, 200, 150, 0, PIT}
    ), LINKS(
        {0, 3, NAV_WALK, 70}, {3, 7, NAV_WALK, 70}, {7, 4, NAV_WALK, 70},
        {4, 1, NAV_WALK, 70}, {0, 5, NAV_WALK, 78}, {5, 8, NAV_WALK, 78},
        {8, 6, NAV_WALK, 78}, {6, 1, NAV_WALK, 78}, {3, 2, NAV_WALK, 68},
        {2, 4, NAV_WALK, 68}, {5, 2, NAV_WALK, 62}, {6, 2, NAV_WALK, 62}
    )),
    PLAN("ctf_Nuubia", 0, 1, 2, ROOMS(
        {-630, -30, 64, 180, 170, 0, ROCK},
        {630, 30, 64, 180, 170, 0, ROCK},
        {0, 0, 100, 180, 180, 0, ROCK},
        {-345, 270, 0, 170, 160, 88, HALL},
        {345, -270, 0, 170, 160, 88, HALL},
        {-285, -285, 40, 170, 170, 0, COURTYARD},
        {285, 285, 40, 170, 170, 0, COURTYARD},
        {0, -472, 72, 190, 150, 0, BRIDGE},
        {0, 472, 72, 190, 150, 0, BRIDGE}
    ), LINKS(
        {0, 5, NAV_WALK, 80}, {5, 7, NAV_WALK, 70}, {7, 4, NAV_WALK, 80},
        {4, 1, NAV_WALK, 80}, {0, 3, NAV_WALK, 80}, {3, 8, NAV_WALK, 80},
        {8, 6, NAV_WALK, 70}, {6, 1, NAV_WALK, 80}, {5, 2, NAV_WALK, 72},
        {2, 6, NAV_WALK, 72}, {3, 2, NAV_WALK, 64}, {4, 2, NAV_WALK, 64}
    )),
    PLAN("ctf_Raspberry", 0, 1, 2, ROOMS(
        {-492, -84, 76, 180, 180, 0, COURTYARD},
        {492, -84, 76, 180, 180, 0, COURTYARD},
        {0, 0, 136, 200, 190, 0, ROCK},
        {-294, 180, 36, 160, 170, 0, ROCK},
        {294, 180, 36, 160, 170, 0, ROCK},
        {0, 336, 0, 220, 170, 0, PIT},
        {-240, -300, 112, 170, 160, 0, BRIDGE},
        {240, -300, 112, 170, 160, 0, BRIDGE},
        {0, -456, 148, 170, 140, 0, COURTYARD}
    ), LINKS(
        {0, 3, NAV_WALK, 86}, {3, 5, NAV_WALK, 88}, {5, 4, NAV_WALK, 88},
        {4, 1, NAV_WALK, 86}, {0, 6, NAV_WALK, 74}, {6, 8, NAV_WALK, 66},
        {8, 7, NAV_WALK, 66}, {7, 1, NAV_WALK, 74}, {6, 2, NAV_WALK, 72},
        {2, 7, NAV_WALK, 72}, {5, 2, NAV_JET, 82}
    )),
    PLAN("ctf_Rotten", 0, 1, 2, ROOMS(
        {-500, -162, 100, 170, 170, 0, ROCK},
        {500, -162, 100, 170, 170, 0, ROCK},
        {0, 0, 120, 180, 180, 0, ROCK},
        {-288, 125, 36, 170, 180, 0, COURTYARD},
        {288, 125, 36, 170, 180, 0, COURTYARD},
        {0, 325, 0, 200, 170, 0, PIT},
        {0, -375, 164, 190, 150, 0, BRIDGE},
        {-475, 362, 0, 160, 160, 88, HALL},
        {475, 362, 0, 160, 160, 88, HALL}
    ), LINKS(
        {0, 3, NAV_WALK, 80}, {3, 5, NAV_WALK, 82}, {5, 4, NAV_WALK, 82},
        {4, 1, NAV_WALK, 80}, {0, 6, NAV_WALK, 72}, {6, 1, NAV_WALK, 72},
        {3, 2, NAV_JET, 66}, {4, 2, NAV_JET, 66}, {2, 6, NAV_WALK, 68},
        {3, 7, NAV_WALK, 72}, {7, 5, NAV_WALK, 72}, {4, 8, NAV_WALK, 72},
        {8, 5, NAV_WALK, 72}
    )),
    PLAN("ctf_Ruins", 0, 1, 2, ROOMS(
        {-620, 0, 40, 180, 170, 76, HALL},
        {620, 0, 40, 180, 170, 76, HALL},
        {0, 0, 72, 180, 180, 0, ROCK},
        {-326, -279, 24, 170, 170, 96, HALL},
        {326, -279, 24, 170, 170, 96, HALL},
        {-326, 279, 0, 170, 170, 68, HALL},
        {326, 279, 0, 170, 170, 68, HALL},
        {0, -450, 104, 170, 150, 0, BRIDGE},
        {0, 450, 16, 180, 160, 0, COURTYARD}
    ), LINKS(
        {0, 3, NAV_WALK, 68}, {3, 7, NAV_WALK, 72}, {7, 4, NAV_WALK, 72},
        {4, 1, NAV_WALK, 68}, {0, 5, NAV_WALK, 74}, {5, 8, NAV_WALK, 74},
        {8, 6, NAV_WALK, 74}, {6, 1, NAV_WALK, 74}, {3, 2, NAV_WALK, 68},
        {2, 4, NAV_WALK, 68}, {5, 2, NAV_WALK, 72}, {2, 6, NAV_WALK, 72}
    )),
    PLAN("ctf_Run", 0, 1, 2, ROOMS(
        {-621, 0, 64, 170, 180, 0, COURTYARD},
        {621, 0, 64, 170, 180, 0, COURTYARD},
        {0, 0, 84, 180, 170, 0, ROCK},
        {-331, -223, 48, 180, 150, 0, BRIDGE},
        {331, -223, 48, 180, 150, 0, BRIDGE},
        {0, -317, 96, 190, 140, 0, BRIDGE},
        {-331, 223, 0, 180, 150, 0, PIT},
        {331, 223, 0, 180, 150, 0, PIT},
        {0, 317, 16, 190, 150, 76, HALL}
    ), LINKS(
        {0, 3, NAV_WALK, 92}, {3, 5, NAV_WALK, 82}, {5, 4, NAV_WALK, 82},
        {4, 1, NAV_WALK, 92}, {0, 6, NAV_WALK, 92}, {6, 8, NAV_WALK, 92},
        {8, 7, NAV_WALK, 92}, {7, 1, NAV_WALK, 92}, {3, 2, NAV_WALK, 74},
        {2, 4, NAV_WALK, 74}, {6, 2, NAV_JET, 72}, {7, 2, NAV_JET, 72}
    )),
    PLAN("ctf_Scorpion", 0, 1, 2, ROOMS(
        {-594, 0, 60, 180, 180, 88, HALL},
        {594, 0, 60, 180, 180, 88, HALL},
        {0, 0, 140, 180, 180, 0, ROCK},
        {-334, -290, 128, 170, 170, 0, ROCK},
        {334, -290, 128, 170, 170, 0, ROCK},
        {-334, 290, 28, 170, 170, 0, COURTYARD},
        {334, 290, 28, 170, 170, 0, COURTYARD},
        {0, -486, 196, 170, 160, 0, BRIDGE},
        {0, 486, 0, 200, 150, 80, HALL}
    ), LINKS(
        {0, 3, NAV_WALK, 70}, {3, 7, NAV_WALK, 66}, {7, 4, NAV_WALK, 66},
        {4, 1, NAV_WALK, 70}, {0, 5, NAV_WALK, 78}, {5, 8, NAV_WALK, 80},
        {8, 6, NAV_WALK, 80}, {6, 1, NAV_WALK, 78}, {3, 2, NAV_WALK, 64},
        {2, 4, NAV_WALK, 64}, {5, 2, NAV_JET, 70}, {6, 2, NAV_JET, 70}
    )),
    PLAN("ctf_Snakebite", 0, 1, 2, ROOMS(
        {-645, 150, 24, 170, 170, 84, HALL},
        {645, -150, 24, 170, 170, 84, HALL},
        {0, 0, 40, 210, 170, 0, COURTYARD},
        {-345, -120, 56, 180, 160, 0, ROCK},
        {345, 120, 56, 180, 160, 0, ROCK},
        {-270, 390, 0, 180, 150, 72, HALL},
        {270, -390, 0, 180, 150, 72, HALL},
        {60, 405, 72, 170, 160, 0, BRIDGE},
        {-60, -405, 72, 170, 160, 0, BRIDGE}
    ), LINKS(
        {0, 3, NAV_WALK, 78}, {3, 2, NAV_WALK, 76}, {2, 4, NAV_WALK, 76},
        {4, 1, NAV_WALK, 78}, {0, 5, NAV_WALK, 80}, {5, 7, NAV_WALK, 76},
        {7, 4, NAV_WALK, 74}, {1, 6, NAV_WALK, 80}, {6, 8, NAV_WALK, 76},
        {8, 3, NAV_WALK, 74}, {2, 7, NAV_JET, 64}, {2, 8, NAV_JET, 64}
    )),
    PLAN("ctf_Steel", 0, 1, 2, ROOMS(
        {-588, 0, 48, 180, 170, 72, HALL},
        {588, 0, 48, 180, 170, 72, HALL},
        {0, 0, 76, 180, 170, 96, HALL},
        {-301, -252, 88, 170, 150, 0, BRIDGE},
        {301, -252, 88, 170, 150, 0, BRIDGE},
        {-301, 252, 0, 170, 160, 72, HALL},
        {301, 252, 0, 170, 160, 72, HALL},
        {0, -399, 120, 190, 150, 0, BRIDGE},
        {0, 399, 24, 190, 150, 80, HALL}
    ), LINKS(
        {0, 3, NAV_WALK, 70}, {3, 7, NAV_WALK, 62}, {7, 4, NAV_WALK, 62},
        {4, 1, NAV_WALK, 70}, {0, 5, NAV_WALK, 76}, {5, 8, NAV_WALK, 70},
        {8, 6, NAV_WALK, 70}, {6, 1, NAV_WALK, 76}, {3, 2, NAV_WALK, 66},
        {2, 4, NAV_WALK, 66}, {5, 2, NAV_WALK, 76}, {2, 6, NAV_WALK, 76}
    )),
    PLAN("ctf_Triumph", 0, 1, 2, ROOMS(
        {-608, -128, 48, 180, 170, 80, HALL},
        {608, 128, 48, 180, 170, 80, HALL},
        {0, 0, 68, 200, 160, 0, COURTYARD},
        {-288, -360, 32, 170, 160, 0, ROCK},
        {288, 360, 32, 170, 160, 0, ROCK},
        {-304, 208, 0, 170, 170, 76, HALL},
        {304, -208, 0, 170, 170, 76, HALL},
        {0, -544, 72, 170, 150, 0, BRIDGE},
        {0, 544, 72, 170, 150, 0, BRIDGE}
    ), LINKS(
        {0, 3, NAV_WALK, 74}, {3, 7, NAV_WALK, 70}, {7, 6, NAV_WALK, 76},
        {6, 1, NAV_WALK, 80}, {0, 5, NAV_WALK, 80}, {5, 8, NAV_WALK, 76},
        {8, 4, NAV_WALK, 70}, {4, 1, NAV_WALK, 74}, {3, 2, NAV_WALK, 68},
        {2, 4, NAV_WALK, 68}, {5, 2, NAV_WALK, 72}, {2, 6, NAV_WALK, 72}
    )),
    PLAN("ctf_Viet", 0, 1, 2, ROOMS(
        {-726, 132, 20, 180, 170, 0, COURTYARD},
        {726, 132, 20, 180, 170, 0, COURTYARD},
        {0, -165, 124, 220, 180, 0, ROCK},
        {-363, -214, 72, 170, 170, 0, ROCK},
        {363, -214, 72, 170, 170, 0, ROCK},
        {0, 198, 36, 190, 160, 96, HALL},
        {-355, 478, 0, 180, 160, 0, PIT},
        {355, 478, 0, 180, 160, 0, PIT},
        {0, 619, 12, 200, 150, 80, HALL}
    ), LINKS(
        {0, 3, NAV_WALK, 84}, {3, 2, NAV_WALK, 72}, {2, 4, NAV_WALK, 72},
        {4, 1, NAV_WALK, 84}, {0, 6, NAV_WALK, 84}, {6, 8, NAV_WALK, 88},
        {8, 7, NAV_WALK, 88}, {7, 1, NAV_WALK, 84}, {3, 5, NAV_WALK, 76},
        {5, 4, NAV_WALK, 76}, {6, 5, NAV_WALK, 76}, {5, 7, NAV_WALK, 76},
        {5, 2, NAV_WALK, 70}
    )),
    PLAN("ctf_Wretch", 0, 1, 2, ROOMS(
        {-604, -248, 112, 180, 170, 0, ROCK},
        {604, -248, 112, 180, 170, 0, ROCK},
        {0, 248, 0, 200, 180, 0, PIT},
        {-341, 0, 56, 170, 170, 0, COURTYARD},
        {341, 0, 56, 170, 170, 0, COURTYARD},
        {0, -217, 124, 180, 180, 0, ROCK},
        {-465, 403, 24, 170, 160, 76, HALL},
        {465, 403, 24, 170, 160, 76, HALL},
        {0, 566, 44, 200, 160, 0, BRIDGE}
    ), LINKS(
        {0, 3, NAV_WALK, 74}, {3, 2, NAV_WALK, 82}, {2, 4, NAV_WALK, 82},
        {4, 1, NAV_WALK, 74}, {0, 5, NAV_WALK, 66}, {5, 1, NAV_WALK, 66},
        {3, 6, NAV_WALK, 78}, {6, 8, NAV_WALK, 76}, {8, 7, NAV_WALK, 76},
        {7, 4, NAV_WALK, 78}, {2, 5, NAV_JET, 74}, {2, 8, NAV_WALK, 80}
    )),
    PLAN("ctf_X", 0, 1, 2, ROOMS(
        {-356, 276, 0, 180, 160, 84, HALL},
        {356, -276, 96, 180, 160, 0, ROCK},
        {0, 0, 48, 200, 190, 0, COURTYARD},
        {-345, -264, 96, 180, 160, 0, ROCK},
        {345, 264, 0, 180, 160, 84, HALL},
        {-230, 0, 44, 160, 170, 80, HALL},
        {230, 0, 44, 160, 170, 80, HALL},
        {0, -414, 116, 170, 150, 0, BRIDGE},
        {0, 414, 0, 170, 150, 76, HALL}
    ), LINKS(
        {0, 5, NAV_WALK, 86}, {5, 2, NAV_WALK, 82}, {2, 6, NAV_WALK, 82},
        {6, 1, NAV_WALK, 86}, {5, 3, NAV_WALK, 82}, {3, 7, NAV_WALK, 72},
        {7, 1, NAV_WALK, 72}, {6, 4, NAV_WALK, 82}, {4, 8, NAV_WALK, 72},
        {8, 0, NAV_WALK, 72}, {2, 7, NAV_WALK, 74}, {2, 8, NAV_WALK, 74}
    )),
    PLAN("inf_Abel", 0, 1, 2, ROOMS(
        {735, 350, 0, 190, 180, 0, COURTYARD},
        {-630, -350, 168, 170, 180, 92, HALL},
        {0, 52, 48, 210, 180, 0, ROCK},
        {385, 210, 16, 180, 170, 0, BRIDGE},
        {-350, -35, 92, 170, 160, 0, ROCK},
        {-630, 175, 68, 160, 180, 84, HALL},
        {-315, -438, 140, 170, 170, 0, BRIDGE},
        {70, -385, 92, 180, 160, 0, COURTYARD},
        {332, -158, 40, 170, 160, 0, PIT}
    ), LINKS(
        {0, 3, NAV_WALK, 92}, {3, 2, NAV_WALK, 84}, {2, 4, NAV_WALK, 74},
        {4, 6, NAV_WALK, 70}, {6, 1, NAV_WALK, 64}, {2, 5, NAV_WALK, 84},
        {5, 1, NAV_WALK, 72}, {3, 8, NAV_WALK, 78}, {8, 7, NAV_WALK, 76},
        {7, 6, NAV_WALK, 72}, {4, 1, NAV_WALK, 60}, {5, 4, NAV_WALK, 68}
    )),
    PLAN("inf_April", 0, 1, 2, ROOMS(
        {645, 240, 20, 190, 170, 0, COURTYARD},
        {-525, -255, 120, 180, 190, 0, ROCK},
        {0, 210, 28, 200, 190, 0, PIT},
        {345, 30, 60, 170, 170, 0, ROCK},
        {-270, 30, 72, 180, 160, 0, COURTYARD},
        {-540, 240, 52, 170, 180, 88, HALL},
        {-195, -330, 132, 170, 160, 0, BRIDGE},
        {150, -285, 84, 190, 170, 0, ROCK},
        {300, 495, 0, 170, 150, 76, HALL}
    ), LINKS(
        {0, 3, NAV_WALK, 84}, {3, 2, NAV_WALK, 84}, {2, 4, NAV_WALK, 78},
        {4, 1, NAV_WALK, 72}, {2, 5, NAV_WALK, 82}, {5, 1, NAV_WALK, 78},
        {3, 7, NAV_WALK, 76}, {7, 6, NAV_WALK, 70}, {6, 1, NAV_WALK, 66},
        {4, 6, NAV_JET, 64}, {0, 8, NAV_WALK, 86}, {8, 2, NAV_WALK, 82}
    )),
    PLAN("inf_Argy", 0, 1, 2, ROOMS(
        {702, -324, 116, 180, 170, 0, COURTYARD},
        {-648, -306, 132, 190, 180, 88, HALL},
        {0, 36, 68, 200, 170, 0, ROCK},
        {360, -198, 96, 170, 160, 0, ROCK},
        {-333, -63, 108, 170, 170, 80, HALL},
        {324, 360, 28, 170, 180, 0, COURTYARD},
        {-162, 468, 0, 190, 170, 0, PIT},
        {-594, 216, 44, 180, 180, 96, HALL},
        {0, -432, 160, 180, 150, 0, BRIDGE},
        {648, 450, 12, 160, 170, 0, ROCK}
    ), LINKS(
        {0, 3, NAV_WALK, 82}, {3, 2, NAV_WALK, 78}, {2, 4, NAV_WALK, 72},
        {4, 1, NAV_WALK, 68}, {3, 5, NAV_WALK, 82}, {5, 6, NAV_WALK, 82},
        {6, 7, NAV_WALK, 78}, {7, 1, NAV_WALK, 82}, {3, 8, NAV_WALK, 72},
        {8, 1, NAV_WALK, 72}, {2, 8, NAV_JET, 66}, {0, 9, NAV_WALK, 88},
        {9, 5, NAV_WALK, 80}, {7, 4, NAV_WALK, 70}
    )),
    PLAN("inf_Belltower", 0, 1, 2, ROOMS(
        {609, 116, 52, 190, 190, 0, ROCK},
        {-493, -145, 160, 160, 170, 88, HALL},
        {-145, 145, 48, 200, 190, 0, COURTYARD},
        {290, 174, 24, 180, 170, 0, PIT},
        {29, -174, 88, 180, 160, 0, BRIDGE},
        {-478, 188, 80, 180, 160, 96, HALL},
        {-246, -276, 128, 170, 170, 0, COURTYARD},
        {-594, -464, 224, 150, 160, 0, BRIDGE},
        {319, -232, 68, 180, 160, 0, ROCK}
    ), LINKS(
        {0, 3, NAV_WALK, 92}, {3, 2, NAV_WALK, 88}, {2, 5, NAV_WALK, 74},
        {5, 1, NAV_WALK, 74}, {0, 8, NAV_WALK, 82}, {8, 4, NAV_WALK, 76},
        {4, 6, NAV_WALK, 72}, {6, 1, NAV_WALK, 68}, {1, 7, NAV_WALK, 64},
        {2, 6, NAV_JET, 66}, {4, 2, NAV_WALK, 78}, {6, 7, NAV_JET, 60}
    )),
    PLAN("inf_Biologic", 0, 1, 2, ROOMS(
        {-540, 168, 0, 180, 170, 80, HALL},
        {504, -192, 108, 180, 180, 88, HALL},
        {0, 0, 44, 200, 180, 0, COURTYARD},
        {-288, 120, 16, 170, 160, 0, ROCK},
        {264, -84, 80, 170, 170, 0, ROCK},
        {-228, -204, 40, 180, 170, 0, COURTYARD},
        {60, -276, 72, 180, 170, 0, BRIDGE},
        {264, 216, 32, 180, 170, 76, HALL},
        {-96, 264, 12, 180, 160, 0, PIT}
    ), LINKS(
        {0, 3, NAV_WALK, 84}, {3, 2, NAV_WALK, 82}, {2, 4, NAV_WALK, 76},
        {4, 1, NAV_WALK, 70}, {0, 5, NAV_WALK, 80}, {5, 6, NAV_WALK, 76},
        {6, 1, NAV_WALK, 76}, {3, 8, NAV_WALK, 84}, {8, 7, NAV_WALK, 82},
        {7, 1, NAV_WALK, 82}, {2, 6, NAV_WALK, 74}, {2, 7, NAV_WALK, 80}
    )),
    PLAN("inf_Changeling", 0, 1, 2, ROOMS(
        {-540, -162, 88, 180, 180, 0, ROCK},
        {513, 108, 48, 190, 180, 92, HALL},
        {0, 0, 64, 190, 170, 0, ROCK},
        {-270, 40, 44, 170, 180, 0, COURTYARD},
        {243, 40, 52, 170, 170, 76, HALL},
        {-176, -310, 108, 180, 170, 0, BRIDGE},
        {176, -338, 128, 170, 160, 0, ROCK},
        {-256, 310, 0, 180, 160, 0, PIT},
        {94, 364, 24, 180, 160, 88, HALL},
        {405, 385, 20, 170, 160, 0, COURTYARD}
    ), LINKS(
        {0, 3, NAV_WALK, 82}, {3, 2, NAV_WALK, 74}, {2, 4, NAV_WALK, 70},
        {4, 1, NAV_WALK, 64}, {0, 5, NAV_WALK, 72}, {5, 6, NAV_WALK, 66},
        {6, 1, NAV_WALK, 80}, {3, 7, NAV_WALK, 84}, {7, 8, NAV_WALK, 82},
        {8, 9, NAV_WALK, 76}, {9, 1, NAV_WALK, 74}, {2, 6, NAV_JET, 62},
        {8, 4, NAV_WALK, 74}
    )),
    PLAN("inf_Flute", 0, 1, 2, ROOMS(
        {243, -270, 100, 200, 180, 0, COURTYARD},
        {284, 310, 0, 180, 180, 84, HALL},
        {0, 14, 52, 200, 180, 0, ROCK},
        {540, -27, 72, 170, 180, 0, ROCK},
        {526, 297, 32, 170, 170, 80, HALL},
        {-270, -202, 84, 180, 170, 0, BRIDGE},
        {-513, 40, 44, 170, 180, 0, ROCK},
        {-284, 310, 0, 180, 170, 76, HALL},
        {14, 446, 0, 180, 150, 0, PIT}
    ), LINKS(
        {0, 3, NAV_WALK, 78}, {3, 4, NAV_WALK, 74}, {4, 1, NAV_WALK, 70},
        {0, 2, NAV_WALK, 78}, {2, 1, NAV_WALK, 76}, {0, 5, NAV_WALK, 82},
        {5, 6, NAV_WALK, 80}, {6, 7, NAV_WALK, 80}, {7, 8, NAV_WALK, 78},
        {8, 1, NAV_WALK, 72}, {5, 2, NAV_WALK, 76}, {7, 2, NAV_WALK, 70}
    )),
    PLAN("inf_Fortress", 0, 1, 2, ROOMS(
        {-792, 0, 0, 200, 190, 0, COURTYARD},
        {0, -306, 196, 190, 190, 104, HALL},
        {0, 234, 32, 220, 200, 0, COURTYARD},
        {-396, 72, 16, 180, 170, 0, ROCK},
        {396, 72, 16, 180, 170, 0, ROCK},
        {774, 0, 0, 190, 180, 0, COURTYARD},
        {-360, -342, 112, 170, 180, 0, ROCK},
        {360, -342, 112, 170, 180, 0, ROCK},
        {0, -702, 152, 190, 160, 0, BRIDGE},
        {0, 639, 0, 210, 170, 0, PIT},
        {-720, -468, 52, 170, 180, 0, COURTYARD},
        {720, -468, 52, 170, 180, 0, COURTYARD}
    ), LINKS(
        {0, 3, NAV_WALK, 92}, {3, 2, NAV_WALK, 88}, {2, 4, NAV_WALK, 88},
        {4, 5, NAV_WALK, 92}, {0, 10, NAV_WALK, 82}, {10, 6, NAV_WALK, 76},
        {6, 1, NAV_WALK, 68}, {5, 11, NAV_WALK, 82}, {11, 7, NAV_WALK, 76},
        {7, 1, NAV_WALK, 68}, {6, 8, NAV_WALK, 72}, {8, 7, NAV_WALK, 72},
        {2, 9, NAV_WALK, 92}, {9, 0, NAV_WALK, 96}, {9, 5, NAV_WALK, 96},
        {3, 6, NAV_JET, 76}, {4, 7, NAV_JET, 76}
    )),
    PLAN("inf_Industrial", 0, 1, 2, ROOMS(
        {-462, 0, 0, 180, 190, 0, COURTYARD},
        {198, 0, 28, 200, 190, 72, HALL},
        {-55, 0, 12, 180, 180, 68, HALL},
        {-253, 0, 0, 160, 170, 0, COURTYARD},
        {-143, -220, 0, 180, 170, 80, HALL},
        {110, -231, 12, 180, 160, 76, HALL},
        {363, -209, 12, 170, 170, 0, COURTYARD},
        {-110, 231, 0, 180, 170, 64, HALL},
        {165, 242, 12, 180, 170, 64, HALL},
        {440, 176, 0, 180, 180, 0, COURTYARD}
    ), LINKS(
        {0, 3, NAV_WALK, 92}, {3, 2, NAV_WALK, 86}, {2, 1, NAV_WALK, 74},
        {0, 4, NAV_WALK, 86}, {4, 5, NAV_WALK, 78}, {5, 6, NAV_WALK, 74},
        {6, 1, NAV_WALK, 70}, {3, 7, NAV_WALK, 88}, {7, 8, NAV_WALK, 82},
        {8, 9, NAV_WALK, 80}, {9, 1, NAV_WALK, 74}, {5, 2, NAV_WALK, 72},
        {8, 2, NAV_WALK, 72}
    )),
    PLAN("inf_Messner", 0, 1, 2, ROOMS(
        {-602, 252, 0, 190, 170, 0, COURTYARD},
        {560, -238, 196, 170, 180, 0, ROCK},
        {0, 14, 92, 190, 180, 0, ROCK},
        {-294, 140, 44, 170, 170, 0, ROCK},
        {294, -98, 144, 170, 170, 0, BRIDGE},
        {-238, -252, 80, 180, 170, 0, COURTYARD},
        {70, -350, 144, 170, 160, 0, ROCK},
        {252, 266, 84, 180, 170, 84, HALL},
        {532, 182, 132, 170, 170, 0, COURTYARD}
    ), LINKS(
        {0, 3, NAV_WALK, 90}, {3, 2, NAV_WALK, 84}, {2, 4, NAV_WALK, 76},
        {4, 1, NAV_WALK, 70}, {0, 5, NAV_WALK, 86}, {5, 6, NAV_WALK, 78},
        {6, 1, NAV_WALK, 76}, {3, 7, NAV_WALK, 88}, {7, 8, NAV_WALK, 80},
        {8, 1, NAV_WALK, 78}, {2, 6, NAV_WALK, 70}, {2, 7, NAV_WALK, 76}
    )),
    PLAN("inf_Moonshine", 0, 1, 2, ROOMS(
        {572, 156, 0, 190, 170, 0, COURTYARD},
        {-507, -91, 80, 190, 180, 0, ROCK},
        {0, 65, 28, 210, 170, 0, COURTYARD},
        {312, 104, 8, 170, 160, 0, ROCK},
        {-266, 39, 52, 180, 170, 0, ROCK},
        {234, -208, 40, 180, 170, 0, BRIDGE},
        {-91, -273, 68, 190, 150, 0, BRIDGE},
        {-234, 312, 8, 180, 170, 76, HALL},
        {117, 351, 0, 180, 150, 0, PIT}
    ), LINKS(
        {0, 3, NAV_WALK, 92}, {3, 2, NAV_WALK, 88}, {2, 4, NAV_WALK, 80},
        {4, 1, NAV_WALK, 72}, {0, 5, NAV_WALK, 84}, {5, 6, NAV_WALK, 76},
        {6, 1, NAV_WALK, 76}, {3, 8, NAV_WALK, 86}, {8, 7, NAV_WALK, 82},
        {7, 1, NAV_WALK, 82}, {2, 6, NAV_WALK, 78}, {2, 7, NAV_WALK, 80}
    )),
    PLAN("inf_Motheaten", 0, 1, 2, ROOMS(
        {566, -290, 112, 180, 170, 0, ROCK},
        {-551, 188, 24, 180, 180, 92, HALL},
        {0, 0, 48, 200, 180, 80, HALL},
        {290, -218, 76, 170, 160, 0, COURTYARD},
        {-290, 0, 36, 170, 170, 72, HALL},
        {261, 261, 12, 170, 170, 0, PIT},
        {-102, 362, 0, 180, 160, 80, HALL},
        {-319, -319, 104, 180, 170, 0, BRIDGE},
        {29, -435, 128, 180, 160, 0, ROCK},
        {-609, -203, 72, 170, 170, 0, COURTYARD}
    ), LINKS(
        {0, 3, NAV_WALK, 80}, {3, 2, NAV_WALK, 76}, {2, 4, NAV_WALK, 72},
        {4, 1, NAV_WALK, 68}, {3, 5, NAV_WALK, 84}, {5, 6, NAV_WALK, 78},
        {6, 1, NAV_WALK, 76}, {0, 8, NAV_WALK, 74}, {8, 7, NAV_WALK, 70},
        {7, 9, NAV_WALK, 72}, {9, 1, NAV_WALK, 80}, {7, 4, NAV_WALK, 76},
        {2, 8, NAV_WALK, 62}
    )),
    PLAN("inf_Rescue", 0, 1, 2, ROOMS(
        {700, 280, 36, 180, 180, 80, HALL},
        {-665, -245, 160, 180, 180, 96, HALL},
        {0, 228, 0, 220, 170, 0, PIT},
        {385, 140, 20, 170, 170, 0, COURTYARD},
        {-350, 70, 76, 180, 170, 0, ROCK},
        {-298, -350, 124, 170, 160, 0, BRIDGE},
        {122, -262, 76, 180, 170, 0, COURTYARD},
        {490, -298, 44, 170, 180, 0, ROCK},
        {-682, 245, 112, 170, 180, 84, HALL}
    ), LINKS(
        {0, 3, NAV_WALK, 84}, {3, 2, NAV_WALK, 90}, {2, 4, NAV_WALK, 82},
        {4, 1, NAV_WALK, 76}, {0, 7, NAV_WALK, 82}, {7, 6, NAV_WALK, 78},
        {6, 5, NAV_WALK, 72}, {5, 1, NAV_WALK, 68}, {4, 8, NAV_WALK, 72},
        {8, 1, NAV_WALK, 72}, {2, 6, NAV_WALK, 82}, {4, 5, NAV_WALK, 70}
    )),
    PLAN("inf_Rise", 0, 1, 2, ROOMS(
        {-589, 356, 0, 180, 180, 0, COURTYARD},
        {294, -434, 228, 180, 170, 96, HALL},
        {-31, 170, 52, 180, 170, 84, HALL},
        {-310, 232, 20, 170, 170, 0, ROCK},
        {294, 155, 76, 180, 170, 88, HALL},
        {558, -108, 132, 170, 170, 0, BRIDGE},
        {279, -155, 176, 180, 160, 88, HALL},
        {-78, -170, 132, 170, 170, 0, ROCK},
        {-388, -124, 84, 170, 170, 0, COURTYARD},
        {0, 527, 24, 180, 160, 80, HALL},
        {574, -434, 204, 160, 170, 0, BRIDGE}
    ), LINKS(
        {0, 3, NAV_WALK, 88}, {3, 2, NAV_WALK, 82}, {2, 4, NAV_WALK, 76},
        {4, 5, NAV_WALK, 76}, {5, 6, NAV_WALK, 70}, {6, 1, NAV_WALK, 66},
        {3, 8, NAV_WALK, 82}, {8, 7, NAV_WALK, 74}, {7, 6, NAV_WALK, 70},
        {0, 9, NAV_WALK, 88}, {9, 4, NAV_WALK, 82}, {5, 10, NAV_WALK, 70},
        {10, 1, NAV_WALK, 62}, {2, 7, NAV_WALK, 64}
    )),
    PLAN("inf_Warehouse", 0, 1, 2, ROOMS(
        {526, -270, 144, 180, 180, 0, COURTYARD},
        {-486, 189, 24, 190, 180, 104, HALL},
        {0, 54, 72, 210, 190, 112, HALL},
        {270, -108, 104, 180, 170, 0, BRIDGE},
        {-243, 68, 48, 170, 170, 92, HALL},
        {-378, -256, 72, 190, 170, 0, COURTYARD},
        {-54, -297, 112, 180, 160, 0, BRIDGE},
        {202, 324, 24, 180, 170, 88, HALL},
        {-135, 392, 0, 180, 170, 80, HALL},
        {526, 162, 72, 180, 180, 0, ROCK}
    ), LINKS(
        {0, 3, NAV_WALK, 80}, {3, 2, NAV_WALK, 78}, {2, 4, NAV_WALK, 72},
        {4, 1, NAV_WALK, 70}, {0, 6, NAV_WALK, 76}, {6, 5, NAV_WALK, 72},
        {5, 1, NAV_WALK, 78}, {0, 9, NAV_WALK, 80}, {9, 7, NAV_WALK, 84},
        {7, 8, NAV_WALK, 78}, {8, 1, NAV_WALK, 76}, {6, 2, NAV_WALK, 70},
        {7, 2, NAV_WALK, 78}
    )),
    PLAN("inf_Warlock", 0, 1, 2, ROOMS(
        {-676, 0, 100, 180, 180, 0, ROCK},
        {330, -182, 88, 190, 180, 104, HALL},
        {0, 182, 28, 200, 180, 0, PIT},
        {-363, -248, 124, 170, 170, 0, COURTYARD},
        {-280, 264, 64, 170, 180, 0, ROCK},
        {33, -412, 156, 180, 170, 0, BRIDGE},
        {644, -231, 128, 170, 170, 0, ROCK},
        {594, 248, 52, 170, 170, 92, HALL},
        {165, 544, 0, 190, 170, 0, COURTYARD},
        {-198, 578, 16, 170, 150, 80, HALL}
    ), LINKS(
        {0, 3, NAV_WALK, 74}, {3, 5, NAV_WALK, 70}, {5, 1, NAV_WALK, 72},
        {0, 4, NAV_WALK, 82}, {4, 2, NAV_WALK, 78}, {2, 1, NAV_WALK, 78},
        {5, 6, NAV_WALK, 72}, {6, 1, NAV_WALK, 64}, {1, 7, NAV_WALK, 72},
        {7, 8, NAV_WALK, 80}, {8, 9, NAV_WALK, 80}, {9, 4, NAV_WALK, 82},
        {2, 8, NAV_WALK, 84}, {2, 5, NAV_JET, 66}
    )),
    PLAN("htf_Arch", 0, 1, 2, ROOMS(
        {-480, 224, 32, 180, 180, 0, COURTYARD},
        {480, 224, 32, 180, 180, 0, COURTYARD},
        {0, -80, 128, 180, 170, 0, ROCK},
        {-368, -144, 84, 170, 170, 0, ROCK},
        {368, -144, 84, 170, 170, 0, ROCK},
        {-288, -464, 160, 170, 160, 0, BRIDGE},
        {288, -464, 160, 170, 160, 0, BRIDGE},
        {0, 336, 52, 180, 170, 0, PIT},
        {0, 688, 0, 200, 170, 84, HALL},
        {0, -720, 200, 180, 160, 0, ROCK}
    ), LINKS(
        {0, 3, NAV_WALK, 78}, {3, 5, NAV_WALK, 70}, {5, 9, NAV_WALK, 66},
        {9, 6, NAV_WALK, 66}, {6, 4, NAV_WALK, 70}, {4, 1, NAV_WALK, 78},
        {0, 7, NAV_WALK, 82}, {7, 1, NAV_WALK, 82}, {7, 8, NAV_WALK, 88},
        {3, 2, NAV_WALK, 70}, {2, 4, NAV_WALK, 70}, {7, 2, NAV_JET, 72},
        {5, 2, NAV_WALK, 62}, {6, 2, NAV_WALK, 62}
    )),
    PLAN("htf_Baire", 0, 1, 2, ROOMS(
        {-560, 272, 24, 180, 170, 0, COURTYARD},
        {560, -272, 88, 180, 170, 0, COURTYARD},
        {0, 0, 120, 220, 190, 0, ROCK},
        {-320, -160, 68, 170, 180, 0, ROCK},
        {320, 160, 56, 170, 180, 0, ROCK},
        {-240, 512, 0, 180, 150, 0, PIT},
        {240, -512, 128, 180, 150, 0, BRIDGE},
        {-608, -496, 104, 170, 160, 0, ROCK},
        {608, 496, 20, 170, 160, 80, HALL}
    ), LINKS(
        {0, 3, NAV_WALK, 80}, {3, 7, NAV_WALK, 72}, {7, 6, NAV_WALK, 76},
        {6, 1, NAV_WALK, 74}, {1, 4, NAV_WALK, 80}, {4, 8, NAV_WALK, 78},
        {8, 5, NAV_WALK, 82}, {5, 0, NAV_WALK, 82}, {3, 2, NAV_WALK, 72},
        {2, 4, NAV_WALK, 72}, {5, 2, NAV_JET, 70}, {6, 2, NAV_WALK, 68}
    )),
    PLAN("htf_Boxed", 0, 1, 2, ROOMS(
        {-405, -378, 64, 180, 180, 88, HALL},
        {405, 378, 0, 180, 180, 88, HALL},
        {0, 0, 96, 200, 180, 0, ROCK},
        {405, -378, 64, 180, 180, 88, HALL},
        {-405, 378, 0, 180, 180, 88, HALL},
        {0, -378, 64, 180, 150, 76, HALL},
        {405, 0, 32, 150, 180, 76, HALL},
        {0, 378, 0, 180, 150, 76, HALL},
        {-405, 0, 32, 150, 180, 76, HALL}
    ), LINKS(
        {0, 5, NAV_WALK, 72}, {5, 3, NAV_WALK, 72}, {3, 6, NAV_WALK, 72},
        {6, 1, NAV_WALK, 72}, {1, 7, NAV_WALK, 72}, {7, 4, NAV_WALK, 72},
        {4, 8, NAV_WALK, 72}, {8, 0, NAV_WALK, 72}, {5, 2, NAV_WALK, 66},
        {7, 2, NAV_WALK, 74}, {6, 2, NAV_WALK, 62}, {8, 2, NAV_WALK, 62}
    )),
    PLAN("htf_Desert", 0, 1, 2, ROOMS(
        {-532, 210, 0, 190, 180, 0, COURTYARD},
        {490, -252, 84, 190, 180, 0, COURTYARD},
        {140, 0, 128, 180, 180, 0, ROCK},
        {-238, -56, 60, 210, 150, 0, BRIDGE},
        {-210, 308, 24, 210, 150, 0, ROCK},
        {-196, -364, 104, 210, 150, 0, BRIDGE},
        {490, 196, 32, 170, 180, 0, ROCK},
        {140, 448, 0, 190, 150, 0, PIT},
        {140, -490, 140, 170, 150, 0, COURTYARD}
    ), LINKS(
        {0, 3, NAV_WALK, 86}, {3, 5, NAV_WALK, 80}, {5, 8, NAV_WALK, 70},
        {8, 1, NAV_WALK, 70}, {1, 6, NAV_WALK, 84}, {6, 7, NAV_WALK, 84},
        {7, 4, NAV_WALK, 82}, {4, 0, NAV_WALK, 88}, {3, 2, NAV_WALK, 72},
        {2, 6, NAV_WALK, 78}, {5, 2, NAV_WALK, 72}, {4, 2, NAV_JET, 68}
    )),
    PLAN("htf_Dorothy", 0, 1, 2, ROOMS(
        {-476, 0, 36, 180, 180, 0, COURTYARD},
        {476, 0, 36, 180, 180, 0, COURTYARD},
        {0, -98, 112, 180, 190, 0, ROCK},
        {-245, -266, 84, 170, 160, 0, BRIDGE},
        {245, -266, 84, 170, 160, 0, BRIDGE},
        {-280, 252, 16, 170, 170, 80, HALL},
        {280, 252, 16, 170, 170, 80, HALL},
        {0, 406, 0, 210, 150, 0, PIT},
        {0, -525, 152, 180, 150, 0, ROCK}
    ), LINKS(
        {0, 3, NAV_WALK, 80}, {3, 8, NAV_WALK, 70}, {8, 4, NAV_WALK, 70},
        {4, 1, NAV_WALK, 80}, {0, 5, NAV_WALK, 82}, {5, 7, NAV_WALK, 84},
        {7, 6, NAV_WALK, 84}, {6, 1, NAV_WALK, 82}, {3, 2, NAV_WALK, 70},
        {2, 4, NAV_WALK, 70}, {5, 2, NAV_WALK, 66}, {6, 2, NAV_WALK, 66}
    )),
    PLAN("htf_Dusk", 0, 1, 2, ROOMS(
        {-496, 208, 0, 170, 170, 84, HALL},
        {496, -208, 48, 170, 170, 84, HALL},
        {0, 0, 32, 210, 180, 0, COURTYARD},
        {-240, -208, 48, 170, 170, 84, HALL},
        {240, 208, 0, 170, 170, 84, HALL},
        {-144, 496, 0, 170, 160, 0, PIT},
        {144, -496, 68, 170, 160, 0, ROCK}
    ), LINKS(
        {0, 3, NAV_WALK, 82}, {3, 6, NAV_WALK, 76}, {6, 1, NAV_WALK, 76},
        {1, 4, NAV_WALK, 82}, {4, 5, NAV_WALK, 80}, {5, 0, NAV_WALK, 80},
        {0, 2, NAV_WALK, 82}, {1, 2, NAV_WALK, 78}, {3, 2, NAV_WALK, 76},
        {4, 2, NAV_WALK, 76}
    )),
    PLAN("htf_Erbium", 0, 1, 2, ROOMS(
        {-566, 203, 0, 180, 170, 0, ROCK},
        {508, -218, 120, 180, 170, 0, ROCK},
        {0, 29, 84, 180, 180, 0, COURTYARD},
        {-261, 116, 44, 170, 180, 0, ROCK},
        {246, -29, 96, 170, 170, 0, ROCK},
        {-348, -261, 100, 180, 170, 0, BRIDGE},
        {0, -377, 152, 180, 160, 0, ROCK},
        {319, 348, 40, 180, 170, 88, HALL},
        {-87, 435, 12, 180, 170, 0, PIT}
    ), LINKS(
        {0, 3, NAV_WALK, 82}, {3, 2, NAV_WALK, 78}, {2, 4, NAV_WALK, 74},
        {4, 1, NAV_WALK, 72}, {0, 5, NAV_WALK, 84}, {5, 6, NAV_WALK, 76},
        {6, 1, NAV_WALK, 74}, {3, 8, NAV_WALK, 82}, {8, 7, NAV_WALK, 82},
        {7, 1, NAV_WALK, 86}, {2, 6, NAV_WALK, 74}, {2, 7, NAV_WALK, 80}
    )),
    PLAN("htf_Feast", 0, 1, 2, ROOMS(
        {-412, -238, 72, 190, 180, 0, ROCK},
        {412, 238, 0, 190, 180, 0, COURTYARD},
        {0, 0, 52, 210, 190, 0, ROCK},
        {-162, -400, 108, 180, 150, 0, BRIDGE},
        {188, -325, 88, 180, 170, 0, ROCK},
        {425, -88, 48, 170, 180, 0, COURTYARD},
        {162, 412, 0, 180, 160, 0, PIT},
        {-175, 325, 24, 180, 170, 0, ROCK},
        {-438, 88, 36, 170, 180, 0, COURTYARD}
    ), LINKS(
        {0, 3, NAV_WALK, 82}, {3, 4, NAV_WALK, 76}, {4, 5, NAV_WALK, 80},
        {5, 1, NAV_WALK, 84}, {1, 6, NAV_WALK, 86}, {6, 7, NAV_WALK, 82},
        {7, 8, NAV_WALK, 82}, {8, 0, NAV_WALK, 84}, {3, 2, NAV_WALK, 74},
        {5, 2, NAV_WALK, 80}, {7, 2, NAV_WALK, 78}, {8, 2, NAV_WALK, 80}
    )),
    PLAN("htf_Mossy", 0, 1, 2, ROOMS(
        {-432, 284, 24, 180, 180, 0, ROCK},
        {432, -284, 144, 180, 180, 0, ROCK},
        {0, 0, 176, 170, 180, 0, ROCK},
        {-310, -94, 96, 170, 170, 0, BRIDGE},
        {310, 94, 96, 170, 170, 0, BRIDGE},
        {-108, 351, 0, 180, 170, 0, PIT},
        {108, -351, 212, 180, 170, 0, BRIDGE},
        {-500, -405, 164, 160, 170, 0, ROCK},
        {500, 405, 44, 160, 170, 0, ROCK}
    ), LINKS(
        {0, 3, NAV_WALK, 84}, {3, 7, NAV_WALK, 76}, {7, 6, NAV_WALK, 72},
        {6, 1, NAV_WALK, 72}, {1, 4, NAV_WALK, 84}, {4, 8, NAV_WALK, 76},
        {8, 5, NAV_WALK, 82}, {5, 0, NAV_WALK, 82}, {3, 2, NAV_JET, 66},
        {4, 2, NAV_JET, 66}, {6, 2, NAV_WALK, 66}, {5, 2, NAV_JET, 76}
    )),
    PLAN("htf_Muygen", 0, 1, 2, ROOMS(
        {-450, 255, 16, 180, 180, 0, COURTYARD},
        {450, -255, 112, 180, 180, 0, COURTYARD},
        {0, 0, 104, 160, 180, 0, BRIDGE},
        {-270, -180, 80, 170, 180, 0, ROCK},
        {270, 180, 48, 170, 180, 0, ROCK},
        {0, -465, 152, 210, 160, 0, BRIDGE},
        {0, 465, 0, 210, 160, 0, ROCK},
        {-525, -510, 136, 160, 170, 0, ROCK},
        {525, 510, 24, 160, 170, 0, ROCK}
    ), LINKS(
        {0, 3, NAV_WALK, 84}, {3, 7, NAV_WALK, 76}, {7, 5, NAV_WALK, 74},
        {5, 1, NAV_WALK, 76}, {1, 4, NAV_WALK, 86}, {4, 8, NAV_WALK, 80},
        {8, 6, NAV_WALK, 80}, {6, 0, NAV_WALK, 84}, {3, 2, NAV_WALK, 70},
        {2, 4, NAV_WALK, 76}, {5, 2, NAV_WALK, 68}, {6, 2, NAV_JET, 68}
    )),
    PLAN("htf_Niall", 0, 1, 2, ROOMS(
        {-300, 200, 8, 160, 160, 88, HALL},
        {300, -200, 96, 160, 160, 88, HALL},
        {0, 0, 64, 180, 180, 0, ROCK},
        {-238, -100, 48, 150, 160, 0, COURTYARD},
        {238, 100, 32, 150, 160, 0, COURTYARD},
        {-25, -325, 120, 170, 150, 0, BRIDGE},
        {25, 325, 0, 170, 150, 0, PIT},
        {-462, -312, 84, 160, 150, 76, HALL},
        {462, 312, 16, 160, 150, 76, HALL}
    ), LINKS(
        {0, 3, NAV_WALK, 70}, {3, 7, NAV_WALK, 64}, {7, 5, NAV_WALK, 68},
        {5, 1, NAV_WALK, 68}, {1, 4, NAV_WALK, 70}, {4, 8, NAV_WALK, 64},
        {8, 6, NAV_WALK, 68}, {6, 0, NAV_WALK, 68}, {3, 2, NAV_WALK, 66},
        {2, 4, NAV_WALK, 66}, {5, 2, NAV_WALK, 62}, {6, 2, NAV_WALK, 74}
    )),
    PLAN("htf_Prison", 0, 1, 2, ROOMS(
        {-363, -253, 56, 180, 170, 88, HALL},
        {363, 253, 0, 180, 170, 88, HALL},
        {0, 0, 32, 180, 180, 96, HALL},
        {-363, 0, 28, 150, 170, 76, HALL},
        {363, 0, 28, 150, 170, 76, HALL},
        {0, -253, 72, 180, 150, 84, HALL},
        {0, 253, 0, 180, 150, 84, HALL},
        {-363, 253, 0, 180, 170, 88, HALL},
        {363, -253, 56, 180, 170, 88, HALL}
    ), LINKS(
        {0, 5, NAV_WALK, 66}, {5, 8, NAV_WALK, 66}, {8, 4, NAV_WALK, 64},
        {4, 1, NAV_WALK, 64}, {1, 6, NAV_WALK, 66}, {6, 7, NAV_WALK, 66},
        {7, 3, NAV_WALK, 64}, {3, 0, NAV_WALK, 64}, {3, 2, NAV_WALK, 62},
        {2, 4, NAV_WALK, 62}, {5, 2, NAV_WALK, 64}, {6, 2, NAV_WALK, 64}
    )),
    PLAN("htf_Rubik", 0, 1, 2, ROOMS(
        {-493, 0, 56, 180, 180, 80, HALL},
        {493, 0, 56, 180, 180, 80, HALL},
        {0, 0, 88, 180, 180, 0, COURTYARD},
        {-246, -276, 112, 160, 160, 0, ROCK},
        {246, -276, 112, 160, 160, 0, ROCK},
        {-246, 276, 24, 160, 160, 88, HALL},
        {246, 276, 24, 160, 160, 88, HALL},
        {0, -536, 168, 170, 160, 0, BRIDGE},
        {0, 536, 0, 170, 160, 0, PIT}
    ), LINKS(
        {0, 3, NAV_WALK, 72}, {3, 7, NAV_WALK, 68}, {7, 4, NAV_WALK, 68},
        {4, 1, NAV_WALK, 72}, {0, 5, NAV_WALK, 74}, {5, 8, NAV_WALK, 74},
        {8, 6, NAV_WALK, 74}, {6, 1, NAV_WALK, 74}, {3, 2, NAV_WALK, 66},
        {2, 4, NAV_WALK, 66}, {5, 2, NAV_WALK, 70}, {2, 6, NAV_WALK, 70},
        {2, 7, NAV_JET, 62}
    )),
    PLAN("htf_Star", 0, 1, 2, ROOMS(
        {-493, -130, 72, 180, 170, 0, COURTYARD},
        {493, -130, 72, 180, 170, 0, COURTYARD},
        {0, 0, 108, 190, 190, 0, ROCK},
        {0, -435, 188, 170, 180, 0, BRIDGE},
        {-319, 362, 12, 180, 170, 0, ROCK},
        {319, 362, 12, 180, 170, 0, ROCK},
        {0, 580, 0, 180, 150, 0, PIT},
        {-261, -406, 140, 150, 160, 0, ROCK},
        {261, -406, 140, 150, 160, 0, ROCK}
    ), LINKS(
        {0, 7, NAV_WALK, 78}, {7, 3, NAV_WALK, 70}, {3, 8, NAV_WALK, 70},
        {8, 1, NAV_WALK, 78}, {0, 4, NAV_WALK, 86}, {4, 6, NAV_WALK, 82},
        {6, 5, NAV_WALK, 82}, {5, 1, NAV_WALK, 86}, {0, 2, NAV_WALK, 74},
        {1, 2, NAV_WALK, 74}, {3, 2, NAV_WALK, 72}, {4, 2, NAV_JET, 68},
        {5, 2, NAV_JET, 68}
    )),
    PLAN("htf_Tower", 0, 1, 2, ROOMS(
        {-540, 216, 24, 180, 180, 0, COURTYARD},
        {540, 216, 24, 180, 180, 0, COURTYARD},
        {0, -144, 160, 170, 180, 0, BRIDGE},
        {-306, -144, 96, 160, 170, 0, ROCK},
        {306, -144, 96, 160, 170, 0, ROCK},
        {0, 342, 48, 180, 170, 104, HALL},
        {-324, -522, 160, 170, 160, 0, ROCK},
        {324, -522, 160, 170, 160, 0, ROCK},
        {0, -774, 212, 170, 160, 0, BRIDGE},
        {0, 738, 0, 190, 160, 0, PIT}
    ), LINKS(
        {0, 3, NAV_WALK, 82}, {3, 6, NAV_WALK, 74}, {6, 8, NAV_WALK, 66},
        {8, 7, NAV_WALK, 66}, {7, 4, NAV_WALK, 74}, {4, 1, NAV_WALK, 82},
        {0, 5, NAV_WALK, 84}, {5, 1, NAV_WALK, 84}, {5, 9, NAV_WALK, 84},
        {3, 2, NAV_WALK, 68}, {2, 4, NAV_WALK, 68}, {5, 2, NAV_WALK, 72},
        {6, 2, NAV_WALK, 64}, {7, 2, NAV_WALK, 64}
    )),
    PLAN("htf_Void", 0, 1, 2, ROOMS(
        {-682, 0, 96, 180, 180, 0, COURTYARD},
        {682, 0, 96, 180, 180, 0, COURTYARD},
        {0, 468, 0, 210, 180, 0, PIT},
        {-351, -292, 196, 170, 190, 0, ROCK},
        {351, -292, 196, 170, 190, 0, ROCK},
        {0, -663, 280, 190, 180, 0, BRIDGE},
        {-429, 448, 52, 170, 180, 0, ROCK},
        {429, 448, 52, 170, 180, 0, ROCK},
        {0, 897, 44, 200, 170, 0, COURTYARD},
        {0, -176, 112, 170, 180, 0, BRIDGE}
    ), LINKS(
        {0, 3, NAV_WALK, 80}, {3, 5, NAV_WALK, 72}, {5, 4, NAV_WALK, 72},
        {4, 1, NAV_WALK, 80}, {0, 6, NAV_WALK, 84}, {6, 2, NAV_WALK, 86},
        {2, 7, NAV_WALK, 86}, {7, 1, NAV_WALK, 84}, {6, 8, NAV_WALK, 86},
        {8, 7, NAV_WALK, 86}, {3, 9, NAV_WALK, 74}, {9, 4, NAV_WALK, 74},
        {9, 2, NAV_WALK, 82}, {2, 5, NAV_JET, 84}
    )),
    PLAN("htf_Vortex", 0, 1, 2, ROOMS(
        {-676, 225, 44, 180, 170, 0, ROCK},
        {554, -471, 164, 180, 170, 0, ROCK},
        {0, 0, 120, 180, 180, 0, BRIDGE},
        {-574, -328, 112, 170, 180, 0, COURTYARD},
        {-123, -676, 192, 180, 160, 0, ROCK},
        {717, 41, 96, 170, 180, 0, COURTYARD},
        {369, 574, 32, 180, 170, 0, ROCK},
        {-164, 717, 0, 180, 170, 0, PIT},
        {-205, 266, 68, 150, 170, 0, ROCK}
    ), LINKS(
        {0, 3, NAV_WALK, 82}, {3, 4, NAV_WALK, 78}, {4, 1, NAV_WALK, 72},
        {1, 5, NAV_WALK, 82}, {5, 6, NAV_WALK, 82}, {6, 7, NAV_WALK, 84},
        {7, 0, NAV_WALK, 86}, {0, 8, NAV_WALK, 78}, {8, 2, NAV_WALK, 72},
        {3, 2, NAV_WALK, 74}, {5, 2, NAV_WALK, 76}, {4, 2, NAV_JET, 64},
        {7, 8, NAV_WALK, 78}
    )),
    PLAN("htf_Zajacz", 0, 1, 2, ROOMS(
        {-609, 116, 28, 190, 170, 0, COURTYARD},
        {566, -174, 100, 190, 170, 0, COURTYARD},
        {0, 0, 112, 200, 190, 0, ROCK},
        {-319, -145, 76, 170, 170, 0, ROCK},
        {290, 145, 60, 170, 170, 0, ROCK},
        {-290, 348, 0, 180, 160, 92, HALL},
        {102, 420, 24, 180, 160, 0, PIT},
        {116, -392, 164, 190, 150, 0, BRIDGE},
        {-261, -435, 136, 170, 160, 0, ROCK},
        {551, 334, 32, 170, 170, 80, HALL}
    ), LINKS(
        {0, 3, NAV_WALK, 84}, {3, 8, NAV_WALK, 76}, {8, 7, NAV_WALK, 72},
        {7, 1, NAV_WALK, 78}, {1, 4, NAV_WALK, 82}, {4, 9, NAV_WALK, 76},
        {9, 6, NAV_WALK, 82}, {6, 5, NAV_WALK, 82}, {5, 0, NAV_WALK, 86},
        {3, 2, NAV_WALK, 70}, {2, 4, NAV_WALK, 74}, {7, 2, NAV_WALK, 70},
        {6, 2, NAV_JET, 70}
    )),
};

const Layout *world_layout_team(const char *name) {
    for (size_t i = 0; i < sizeof(layouts) / sizeof(layouts[0]); ++i)
        if (!strcmp(name, layouts[i].name)) return &layouts[i].layout;
    return NULL;
}
