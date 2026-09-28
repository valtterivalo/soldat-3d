#define MATERIALS(...) ((const LayoutMaterial[]){__VA_ARGS__})
#define MATERIAL_PLAN(name, materials) {name, materials, sizeof(materials) / sizeof(LayoutMaterial)}

static const struct { const char *name; const LayoutMaterial *materials; size_t count; } material_plans[] = {
    MATERIAL_PLAN("Aero", MATERIALS(
        {4, -45, 35, 18, 20, 0, 5, 0},
        {5, 45, -35, 18, 20, 0, 5, 0}
    )),
    {"Airpirates", NULL, 0},
    {"Arena", NULL, 0},
    {"Arena2", NULL, 0},
    {"Arena3", NULL, 0},
    {"Bigfalls", NULL, 0},
    {"Blox", NULL, 0},
    MATERIAL_PLAN("Bridge", MATERIALS(
        {1, 0, 32, 70, 18, 0, 9, 0}
    )),
    MATERIAL_PLAN("Bunker", MATERIALS(
        {5, 40, -35, 40, 30, 0, 9, 0}
    )),
    MATERIAL_PLAN("Cambodia", MATERIALS(
        {1, -35, 35, 14, 14, 0, 6, 0}
    )),
    {"CrackedBoot", NULL, 0},
    {"Daybreak", NULL, 0},
    {"DesertWind", NULL, 0},
    {"Factory", NULL, 0},
    {"Flashback", NULL, 0},
    {"HH", NULL, 0},
    {"Island2k5", NULL, 0},
    MATERIAL_PLAN("Jungle", MATERIALS(
        {1, 45, 40, 28, 22, 0, 6, 0},
        {4, -55, -42, 20, 18, 0, 7, 0},
        {3, 45, 50, 20, 18, 0, 7, 0}
    )),
    {"Krab", NULL, 0},
    {"Lagrange", NULL, 0},
    {"Leaf", NULL, 0},
    {"MrSnowman", NULL, 0},
    {"RR", NULL, 0},
    {"RatCave", NULL, 0},
    {"Rok", NULL, 0},
    MATERIAL_PLAN("Shau", MATERIALS(
        {7, -50, -40, 12, 12, 0, 5, 0},
        {3, 45, 45, 12, 12, 0, 5, 0},
        {8, -50, 45, 12, 12, 0, 6, 0}
    )),
    {"Tropiccave", NULL, 0},
    {"Unlim", NULL, 0},
    {"Veoto", NULL, 0},
    {"ctf_Aftermath", NULL, 0},
    {"ctf_Amnesia", NULL, 0},
    {"ctf_Ash", NULL, 0},
    MATERIAL_PLAN("ctf_B2b", MATERIALS(
        {2, -55, 45, 18, 18, 0, 6, 0},
        {2, 55, -45, 18, 18, 0, 6, 0},
        {0, -45, -45, 18, 18, 0, 5, 0},
        {1, 45, 45, 18, 18, 0, 5, 0},
        {0, -50, 35, 18, 18, 0, 7, 0},
        {1, 50, -35, 18, 18, 0, 7, 0}
    )),
    MATERIAL_PLAN("ctf_Blade", MATERIALS(
        {2, -45, 30, 32, 45, 0, 9, 0},
        {2, 45, -30, 32, 45, 0, 9, 0}
    )),
    MATERIAL_PLAN("ctf_Campeche", MATERIALS(
        {2, 50, 45, 20, 25, 0, 5, 0}
    )),
    MATERIAL_PLAN("ctf_Cobra", MATERIALS(
        {2, 0, 0, 100, 70, 0, 8, 0}
    )),
    MATERIAL_PLAN("ctf_Crucifix", MATERIALS(
        {2, -55, -50, 16, 20, 0, 5, 0},
        {2, 55, 50, 16, 20, 0, 5, 0},
        {0, 32, 0, 40, 70, 0, 20, 0},
        {1, -32, 0, 40, 70, 0, 20, 0},
        {6, 0, 50, 50, 28, 0, 18, 3.5},
        {2, 45, 45, 44, 4, 32, 21, 0}
    )),
    {"ctf_Death", NULL, 0},
    {"ctf_Division", NULL, 0},
    {"ctf_Dropdown", NULL, 0},
    MATERIAL_PLAN("ctf_Equinox", MATERIALS(
        {2, -45, 40, 20, 20, 0, 6, 0},
        {2, 45, -40, 20, 20, 0, 6, 0},
        {0, -55, 45, 16, 24, 0, 5, 0},
        {1, 55, -45, 16, 24, 0, 5, 0}
    )),
    {"ctf_Guardian", NULL, 0},
    {"ctf_Hormone", NULL, 0},
    MATERIAL_PLAN("ctf_IceBeam", MATERIALS(
        {5, 0, 0, 120, 75, 0, 4, 0},
        {2, 0, 30, 4, 8, 96, 2, 0}
    )),
    {"ctf_Kampf", NULL, 0},
    {"ctf_Lanubya", NULL, 0},
    {"ctf_Laos", NULL, 0},
    {"ctf_MFM", NULL, 0},
    {"ctf_Maya", NULL, 0},
    {"ctf_Mayapan", NULL, 0},
    {"ctf_Nuubia", NULL, 0},
    {"ctf_Raspberry", NULL, 0},
    MATERIAL_PLAN("ctf_Rotten", MATERIALS(
        {2, 0, 50, 50, 24, 0, 6, 0}
    )),
    {"ctf_Ruins", NULL, 0},
    {"ctf_Run", NULL, 0},
    {"ctf_Scorpion", NULL, 0},
    MATERIAL_PLAN("ctf_Snakebite", MATERIALS(
        {0, -48, 0, 4, 70, 56, 11, 0},
        {1, 48, 0, 4, 70, 56, 13, 0}
    )),
    {"ctf_Steel", NULL, 0},
    MATERIAL_PLAN("ctf_Triumph", MATERIALS(
        {7, -45, -30, 22, 20, 0, 5, 0},
        {8, 45, 30, 22, 20, 0, 5, 0}
    )),
    {"ctf_Viet", NULL, 0},
    MATERIAL_PLAN("ctf_Voland", MATERIALS(
        {0, 0, 0, 65, 65, 0, 8, 0},
        {1, 0, 0, 65, 65, 0, 8, 0},
        {6, -42, 38, 22, 18, 0, 5, 0},
        {6, 42, 38, 22, 18, 0, 5, 0}
    )),
    {"ctf_Wretch", NULL, 0},
    {"ctf_X", NULL, 0},
    {"htf_Arch", NULL, 0},
    {"htf_Baire", NULL, 0},
    {"htf_Boxed", NULL, 0},
    MATERIAL_PLAN("htf_Desert", MATERIALS(
        {4, 45, -35, 25, 24, 0, 5, 0}
    )),
    {"htf_Dorothy", NULL, 0},
    {"htf_Dusk", NULL, 0},
    {"htf_Erbium", NULL, 0},
    {"htf_Feast", NULL, 0},
    {"htf_Mossy", NULL, 0},
    {"htf_Muygen", NULL, 0},
    MATERIAL_PLAN("htf_Niall", MATERIALS(
        {2, 45, 45, 30, 24, 0, 5, 0}
    )),
    MATERIAL_PLAN("htf_Nuclear", MATERIALS(
        {6, 0, 0, 100, 100, 0, 7, 0}
    )),
    {"htf_Prison", NULL, 0},
    {"htf_Rubik", NULL, 0},
    MATERIAL_PLAN("htf_Star", MATERIALS(
        {4, -45, 35, 32, 30, 0, 18, 5.8},
        {5, 45, 35, 32, 30, 0, 18, 5.8},
        {6, 0, 45, 34, 24, 0, 18, 5.8},
        {0, -55, -35, 28, 28, 0, 18, 6.2},
        {1, 55, -35, 28, 28, 0, 18, 6.2},
        {7, -40, -30, 24, 24, 0, 18, 5.8},
        {8, 40, 30, 24, 24, 0, 18, 5.7909998143}
    )),
    {"htf_Tower", NULL, 0},
    MATERIAL_PLAN("htf_Void", MATERIALS(
        {0, -55, 45, 18, 32, 0, 7, 0}
    )),
    {"htf_Vortex", NULL, 0},
    {"htf_Zajacz", NULL, 0},
    MATERIAL_PLAN("inf_Abel", MATERIALS(
        {0, -45, -40, 26, 22, 0, 9, 0},
        {1, 45, 40, 22, 22, 0, 9, 0},
        {4, 45, 40, 26, 20, 0, 7, 0}
    )),
    MATERIAL_PLAN("inf_April", MATERIALS(
        {2, 45, 40, 32, 28, 0, 7, 0}
    )),
    MATERIAL_PLAN("inf_Argy", MATERIALS(
        {2, -45, 40, 36, 26, 0, 7, 0}
    )),
    {"inf_Belltower", NULL, 0},
    {"inf_Biologic", NULL, 0},
    {"inf_Changeling", NULL, 0},
    {"inf_Flute", NULL, 0},
    MATERIAL_PLAN("inf_Fortress", MATERIALS(
        {1, -55, 50, 24, 24, 0, 6, 0},
        {1, 55, 50, 24, 24, 0, 6, 0},
        {2, -65, 55, 26, 24, 0, 6, 0},
        {2, 65, 55, 26, 24, 0, 6, 0}
    )),
    {"inf_Industrial", NULL, 0},
    {"inf_Messner", NULL, 0},
    MATERIAL_PLAN("inf_Moonshine", MATERIALS(
        {4, -45, -40, 20, 20, 0, 5, 0},
        {6, 45, 40, 20, 20, 0, 5, 0},
        {2, -40, 40, 26, 22, 0, 7, 0}
    )),
    MATERIAL_PLAN("inf_Motheaten", MATERIALS(
        {2, -45, 40, 26, 26, 0, 5, 0},
        {2, 45, 40, 26, 26, 0, 5, 0}
    )),
    MATERIAL_PLAN("inf_Outpost", MATERIALS(
        {8, 50, 40, 36, 14, 0, 5, 0},
        {1, -45, -35, 4, 45, 52, 11, 0}
    )),
    MATERIAL_PLAN("inf_Rescue", MATERIALS(
        {0, 45, -45, 18, 18, 0, 5, 0},
        {2, -55, 45, 35, 25, 0, 7, 0},
        {2, 55, 45, 35, 25, 0, 7, 0},
        {7, 0, 0, 40, 40, 0, 8, 0}
    )),
    {"inf_Rise", NULL, 0},
    MATERIAL_PLAN("inf_Warehouse", MATERIALS(
        {2, -65, 50, 28, 26, 0, 6, 0},
        {2, 65, 50, 28, 26, 0, 6, 0},
        {0, 45, -35, 4, 46, 60, 13, 0},
        {0, 45, -35, 4, 46, 60, 21, 0}
    )),
    MATERIAL_PLAN("inf_Warlock", MATERIALS(
        {6, 0, 0, 85, 65, 0, 8, 0}
    )),
};

#undef MATERIAL_PLAN
#undef MATERIALS

static const LayoutMaterial *layout_material_plan(const char *name, size_t *count)
{
    for (size_t i=0; i<sizeof(material_plans)/sizeof(*material_plans); ++i) {
        if (strcmp(name, material_plans[i].name)) continue;
        *count=material_plans[i].count;
        return material_plans[i].materials;
    }
    fprintf(stderr, "No material plan for %s\n", name);
    abort();
}
