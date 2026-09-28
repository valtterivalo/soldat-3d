#include "interface.h"
#include "generated_rules.h"
#include "world.h"
#include "network.h"
#include "rlgl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static float read_scale(const char *config, const char *key) {
    const char *entry = strstr(config, key);
    float value;
    if (!entry || sscanf(entry + strlen(key), "=%f", &value) != 1) abort();
    return value;
}

static Texture2D texture(const char *name) {
    Texture2D result = LoadTexture(TextFormat("%s/interface-gfx/%s.png", SOLDAT_ASSET_DIR, name));
    if (!IsTextureValid(result)) {
        fprintf(stderr, "Failed to load original interface texture %s\n", name);
        abort();
    }
    SetTextureFilter(result, TEXTURE_FILTER_BILINEAR);
    return result;
}

Interface interface_load(void) {
    Interface interface = {0};
    char *config = LoadFileText(SOLDAT_ASSET_DIR "/mod.ini");
    if (!config) abort();
    interface.asset_scale = read_scale(config, "DefaultScale");
    interface.cursor_scale = read_scale(config, "interface-gfx/cursor.png");
    interface.title_scale = read_scale(config, "interface-gfx/title-l.png");
    UnloadFileText(config);
    interface.font = LoadFontEx(SOLDAT_ASSET_DIR "/interface-gfx/play-regular.ttf", 48, NULL, 0);
    if (!IsFontValid(interface.font)) abort();
    SetTextureFilter(interface.font.texture, TEXTURE_FILTER_BILINEAR);
    const char *files[UI_TEXTURE_COUNT] = {
        "health", "ammo", "jet", "health-bar", "reload-bar", "jet-bar",
        "fire-bar", "fire-bar-r", "nade", "cursor", "menucursor", "back", "title-l", "title-r", "vest-bar", "cluster-nade", "flag", "noflag"
    };
    for (int i = 0; i < UI_TEXTURE_COUNT; ++i) interface.textures[i] = texture(files[i]);
    const char *guns[14] = {
        "guns/1", "guns/2", "guns/3", "guns/4", "guns/5", "guns/6", "guns/7",
        "guns/8", "guns/9", "guns/0", "guns/10", "guns/knife", "guns/chainsaw", "guns/law"
    };
    for (int i = 0; i < 14; ++i) interface.guns[i] = texture(guns[i]);
    return interface;
}

void interface_unload(Interface *interface) {
    for (int i = 0; i < UI_TEXTURE_COUNT; ++i) UnloadTexture(interface->textures[i]);
    for (int i = 0; i < 14; ++i) UnloadTexture(interface->guns[i]);
    UnloadFont(interface->font);
}

static void sprite(Texture2D image, float x, float y, float factor, Color tint) {
    DrawTextureEx(image, (Vector2){x, y}, 0, factor, tint);
}

static void bar(Texture2D image, float x, float y, float factor, float amount, int right) {
    amount = fminf(1, fmaxf(0, amount));
    float offset = right ? image.width * (1 - amount) : 0;
    DrawTexturePro(image, (Rectangle){offset, 0, image.width * amount, image.height},
        (Rectangle){x + offset * factor, y, image.width * amount * factor, image.height * factor},
        (Vector2){0, 0}, 0, WHITE);
}

static void text(const Interface *interface, const char *value, float x, float y,
    float size, float alignment, Color color) {
    float width = MeasureTextEx(interface->font, value, size, 0).x * 1.25f;
    rlPushMatrix();
    rlTranslatef(x - width * alignment, y, 0);
    rlScalef(1.25f, 1, 1);
    DrawTextEx(interface->font, value, (Vector2){1, 1}, size, 0, (Color){0, 0, 0, color.a});
    DrawTextEx(interface->font, value, (Vector2){0, 0}, size, 0, color);
    rlPopMatrix();
}

void interface_hud(const Interface *interface, const Game *game, HitFeedback hit,
    const char *killfeed, int feed_ticks, const char *const names[ACTOR_COUNT], int local_actor, Camera3D camera) {
    const Actor *player = &game->actors[local_actor];
    const WeaponState *gun = &player->slots[player->active_slot];
    const WeaponDef *def = &weapons[gun->id];
    float w = (float)GetScreenWidth(), h = (float)GetScreenHeight(), s = h / 480;
    float factor = s / interface->asset_scale;
    const Color team_colors[] = {WHITE,{210,15,5,255},{21,31,217,255},{210,210,5,255},{5,210,5,255},GRAY};
    if (game_team_mode(game->mode)) {
        int teams=game->mode==MODE_TEAMMATCH ? 4 : 2;
        for (int i=1;i<=teams;++i) {
            text(interface,TextFormat("%s %d",game_team_names[i],game->team_score[i]),
                w*.5f+(i-(teams+1)*.5f)*110*s,32*s,13*s,.5f,team_colors[i]);
            if (game->mode==MODE_CTF || game->mode==MODE_INF) {
                Texture2D icon=interface->textures[game->flags[i-1].state==FLAG_BASE ? UI_FLAG:UI_NOFLAG];
                sprite(icon,w*.5f+(i-(teams+1)*.5f)*110*s-icon.width*factor*.35f,47*s,factor*.7f,team_colors[i]);
            }
        }
    }
    if (game->time_limit_ticks>0) {
        int seconds=(game->time_limit_ticks-game->match_ticks)/TICK_RATE;
        if (seconds<0) seconds=0;
        text(interface,TextFormat("%d:%02d",seconds/60,seconds%60),w*.5f,12*s,12*s,.5f,WHITE);
    }
    for (int i=0;i<3;++i) {
        const Flag *flag=&game->flags[i];
        if (flag->state==FLAG_ABSENT) continue;
        Color tint=i==0 ? team_colors[TEAM_ALPHA] : i==1 ? team_colors[TEAM_BRAVO] : YELLOW;
        Vector3 point={flag->position.x,flag->position.y+24,flag->position.z};
        Vector2 screen=GetWorldToScreen(point,camera);
        Vec3 view=v3(point.x-camera.position.x,point.y-camera.position.y,point.z-camera.position.z);
        Vec3 forward=v3(camera.target.x-camera.position.x,camera.target.y-camera.position.y,camera.target.z-camera.position.z);
        if (dot(view,forward)<=0) screen=(Vector2){screen.x<w/2?w-20*s:20*s,h*.55f};
        screen.x=fmaxf(20*s,fminf(w-20*s,screen.x));
        screen.y=fmaxf(65*s,fminf(h-70*s,screen.y));
        sprite(interface->textures[UI_FLAG],screen.x-5*s,screen.y-14*s,factor*.7f,tint);
        const char *state=flag->state==FLAG_BASE ? "Base" : flag->state==FLAG_DROPPED ? "Dropped" : "Carrier";
        text(interface,state,screen.x,screen.y+2*s,9*s,.5f,tint);
    }
    float health_x = 5 * w / 640, ammo_x = 275 * w / 640, jet_x = w - 160 * s;
    sprite(interface->textures[UI_HEALTH], health_x, 439 * s, factor, WHITE);
    sprite(interface->textures[UI_AMMO], ammo_x, 439 * s, factor, WHITE);
    sprite(interface->textures[UI_JET], jet_x, 439 * s, factor, WHITE);
    bar(interface->textures[UI_HEALTH_BAR], health_x + 40 * s, 449 * s,
        factor, player->health / SRC_DEFAULT_HEALTH, 0);
    if (player->vest > 0)
        bar(interface->textures[UI_VEST_BAR], health_x + 40 * s, 459 * s, factor, player->vest / SRC_DEFAULTVEST, 0);
    float ammo = 0;
    if (gun->id != NOWEAPON) {
        ammo = (float)gun->ammo / def->ammo;
        if (gun->phase == WEAPON_RELOADING && gun->id != SPAS12)
            ammo = 1 - (float)gun->reload_count / def->reload_time;
    }
    bar(interface->textures[UI_RELOAD_BAR], ammo_x + 77 * s, 449 * s, factor, ammo, 0);
    bar(interface->textures[UI_JET_BAR], jet_x + 40 * s, 449 * s,
        factor, player->fuel_capacity == 0 ? 0 : (float)player->fuel / player->fuel_capacity, 0);
    sprite(interface->textures[UI_FIRE_FRAME], ammo_x + 127 * s, 464 * s, factor, WHITE);
    bar(interface->textures[UI_FIRE_BAR], ammo_x + 134 * s, 464 * s,
        factor, (float)gun->fire_count / def->fire_interval, 1);
    text(interface, TextFormat("%d", gun->ammo), ammo_x + 73 * s, 451 * s,
        12 * s, 1, (Color){242, 244, 40, 255});
    text(interface, def->name, ammo_x + 10 * s, 454 * s,
        10.667f * s, 1, (Color){255, 245, 177, 255});
    for (int i = 0; i < player->grenades; ++i)
        sprite(interface->textures[player->grenade_weapon == CLUSTERGRENADE ? UI_CLUSTER_GRENADE : UI_GRENADE],
            ammo_x + (43 + i * 10) * s, 462 * s, factor, WHITE);
    if (player->bonus != BONUS_NONE) {
        const char *bonuses[] = {"", "Flame God", "Predator", "Berserker"};
        text(interface, TextFormat("%s - %.1f", bonuses[player->bonus], (float)player->bonus_ticks / TICK_RATE),
            w / 2, 420 * s, 12 * s, .5f, (Color){255, 55, 50, 255});
    }
    int rank = 1, active = 0;
    for (int i = 0; i < ACTOR_COUNT; ++i) {
        if (game->actors[i].life == INACTIVE) continue;
        ++active;
        if (game->actors[i].kills > player->kills) ++rank;
    }
    text(interface, TextFormat("%d/%d", rank, active), w - 24 * s, 417 * s,
        12 * s, 1, (Color){88, 255, 90, 255});
    text(interface, TextFormat("%d", player->kills), w - 24 * s, 430 * s,
        12 * s, 1, (Color){255, 55, 50, 255});
    if (feed_ticks > 0)
        text(interface, killfeed, w - 10 * s, 14 * s, 12 * s, 1, (Color){230, 232, 255, 255});
    if (player->life == ALIVE) {
        if (player->spawn_protection_ticks>0)
            text(interface,TextFormat("%d",player->spawn_protection_ticks/TICK_RATE+1),w/2,h/2-32*s,16*s,.5f,WHITE);
        float bloom = 1 + powf((float)player->bink_count, .6f) / 20;
        Texture2D cursor = interface->textures[UI_CURSOR];
        float cross_scale = s / interface->cursor_scale * bloom;
        sprite(cursor, (w - cursor.width * cross_scale) / 2, (h - cursor.height * cross_scale) / 2,
            cross_scale, WHITE);
        if (hit.kind != HIT_NONE && hit.age < .15f) {
            float progress = hit.age / .15f;
            float fade = 1 - progress * progress;
            float inner = 9 + 1.5f * (1 - progress) * (1 - progress);
            float outer = inner + (hit.kind == HIT_KILL ? 4 : 3);
            for (int x = -1; x <= 1; x += 2) {
                for (int y = -1; y <= 1; y += 2) {
                    Vector2 from = {w / 2 + x * inner * s, h / 2 + y * inner * s};
                    Vector2 to = {w / 2 + x * outer * s, h / 2 + y * outer * s};
                    DrawLineEx(from, to, 2 * s, (Color){0, 0, 0, (unsigned char)(90 * fade)});
                    DrawLineEx(from, to, s, (Color){255, 255, 255, (unsigned char)(225 * fade)});
                }
            }
        }
    } else if (player->team==TEAM_SPECTATOR) {
        text(interface,"Spectating",w/2,h-70*s,14*s,.5f,WHITE);
    } else {
        text(interface, TextFormat("Respawn in %.1f", (float)player->respawn_ticks / TICK_RATE),
            w / 2, h / 2 - 20 * s, 16 * s, .5f, (Color){255, 90, 95, 255});
    }
    if (!IsKeyDown(KEY_TAB) && game->phase!=MATCH_FINISHED) return;
    float left = (w - 590 * s) / 2;
    float spacing = active==0 ? 20 : fminf(20, 360.0f / active);
    Texture2D back = interface->textures[UI_BACK];
    DrawTexturePro(back, (Rectangle){0, 0, back.width, back.height},
        (Rectangle){left, 5 * s, 590 * s, (55 + spacing * active) * s}, (Vector2){0, 0}, 0,
        (Color){255, 255, 255, 180});
    text(interface, TextFormat("%s - %s%s", game_mode_names[game->mode],
        world_map_names[world_map_current],game->phase==MATCH_FINISHED?" - Match finished":""), left + 15 * s, 12 * s, 14 * s, 0, (Color){114, 120, 255, 255});
    text(interface, "Player", left + 15 * s, 34 * s, 12 * s, 0, WHITE);
    text(interface, "Team", left + 340 * s, 34 * s, 12 * s, 1, WHITE);
    text(interface, "Score", left + 430 * s, 34 * s, 12 * s, 1, WHITE);
    text(interface, "Caps", left + 485 * s, 34 * s, 12 * s, 1, WHITE);
    text(interface, "Deaths", left + 550 * s, 34 * s, 12 * s, 1, WHITE);
    int order[ACTOR_COUNT];
    int count = 0;
    for (int i = 0; i < ACTOR_COUNT; ++i) {
        if (game->actors[i].life == INACTIVE) continue;
        int j = count++;
        while (j > 0 && game->actors[order[j - 1]].kills < game->actors[i].kills) {
            order[j] = order[j - 1];
            --j;
        }
        order[j] = i;
    }
    for (int row = 0; row < count; ++row) {
        int i = order[row];
        Color color = game_team_mode(game->mode) ? team_colors[game->actors[i].team] : WHITE;
        if (i == local_actor) color=(Color){88,255,90,255};
        float y = (54 + row * spacing) * s;
        float size = fminf(12, spacing - 1) * s;
        text(interface, names[i], left + 15 * s, y, size, 0, color);
        text(interface, game_team_names[game->actors[i].team], left + 340 * s, y, size, 1, color);
        text(interface, TextFormat("%d", game->actors[i].kills), left + 430 * s, y, size, 1, color);
        text(interface, TextFormat("%d", game->actors[i].captures), left + 485 * s, y, size, 1, color);
        text(interface, TextFormat("%d", game->actors[i].deaths), left + 550 * s, y, size, 1, color);
    }
}

InterfaceAction interface_menu(const Interface *interface, WeaponId *primary, WeaponId *secondary,
    const InputPresses *presses) {
    float s = (float)GetScreenHeight() / 480;
    float left = ((float)GetScreenWidth() - 640 * s) / 2;
    float factor = s / interface->asset_scale;
    Vector2 mouse = GetMousePosition();
    Texture2D back = interface->textures[UI_BACK];
    for (int section = 0; section < 2; ++section)
        DrawTexturePro(back, (Rectangle){0, 0, back.width, back.height},
            (Rectangle){left + 45 * s, (140 + 210 * section) * s, 252 * s, (section ? 80 : 210) * s},
            (Vector2){0, 0}, 0, (Color){255, 255, 255, 143});
    float title_factor = s * .48f / interface->title_scale;
    Texture2D title_left = interface->textures[UI_TITLE_LEFT];
    sprite(title_left, left + 45 * s, 65 * s, title_factor, WHITE);
    sprite(interface->textures[UI_TITLE_RIGHT], left + 45 * s + title_left.width * title_factor,
        65 * s, title_factor, WHITE);
    text(interface, "Primary Weapon", left + 65 * s, 139 * s, 12 * s, 0, (Color){234, 234, 234, 255});
    text(interface, "Secondary Weapon", left + 65 * s, 347 * s, 12 * s, 0, (Color){214, 214, 214, 255});
    for (int i = 0; i < 14; ++i) {
        float y = (154 + 18 * (i + (i >= 10))) * s;
        Rectangle row = {left + 35 * s, y, 262 * s, 18 * s};
        int hovered = CheckCollisionPointRec(mouse, row);
        int shortcut = presses->keys[i < 10 ? (i == 9 ? KEY_ZERO : KEY_ONE + i) : KEY_F1 + i - 10];
        if ((hovered && (presses->mouse & (1u << MOUSE_BUTTON_LEFT))) || shortcut) {
            if (i < 10) *primary = (WeaponId)i;
            else *secondary = (WeaponId)i;
        }
        int selected = i < 10 ? *primary == (WeaponId)i : *secondary == (WeaponId)i;
        Texture2D gun = interface->guns[i];
        float image_y = y + 3 * s + fmaxf(0, 18 * s - gun.height * factor) / 2;
        sprite(gun, left + 55 * s, image_y, factor,
            (Color){255, 255, 255, i < 10 || selected ? 255 : 128});
        const char *caption = i < 10 ? TextFormat("%d %s", (i + 1) % 10, weapons[i].name) : weapons[i].name;
        Color color = selected ? (Color){55, 165, 55, 230} : (Color){255, 255, 255, 230};
        if (hovered && selected) color = (Color){85, 105, 55, 230};
        text(interface, caption, left + (120 + hovered) * s, y + (6 - hovered) * s, 10.667f * s, 0, color);
    }
    text(interface, "Controls", left + 325 * s, 145 * s, 12 * s, 0, (Color){250, 90, 95, 255});
    const char *controls[] = {
        "[W A S D] move", "[Mouse] aim", "[Left Mouse] fire!", "[Right Mouse] jet boots",
        "[Space] jump  [Shift] roll", "[Ctrl] crouch  [X] lie down", "[Alt] steady Barrett focus", "hold [E] toss grenade",
        "[R] reload  [Q] change weapon", "[F] throw weapon  [V] shoulder", "[Tab] score  [Esc] menu",
        "[1-0] primary  [F1-F4] secondary", "[G] throw flag  [F3] FPS in game"
    };
    for (size_t i = 0; i < sizeof(controls) / sizeof(controls[0]); ++i)
        text(interface, controls[i], left + 325 * s, (165 + i * 16) * s,
            10.667f * s, 0, (Color){230, (unsigned char)(232 - i * 2), 255, 255});
    Rectangle play = {left + 325 * s, 378 * s, 270 * s, 24 * s};
    int hovered = CheckCollisionPointRec(mouse, play);
    text(interface, "[Enter] Continue", play.x, play.y, 14 * s, 0,
        hovered ? (Color){88, 255, 90, 255} : WHITE);
    sprite(interface->textures[UI_MENU_CURSOR], mouse.x, mouse.y, factor, WHITE);
    return presses->keys[KEY_ENTER] || (hovered && (presses->mouse & (1u << MOUSE_BUTTON_LEFT))) ?
        INTERFACE_PLAY : INTERFACE_IDLE;
}

int interface_browser(const Interface *interface,const Lobby *lobby,int *selected,const InputPresses *presses) {
    float w=(float)GetScreenWidth(),h=(float)GetScreenHeight(),s=h/480;
    float left=(w-600*s)/2,top=70*s;
    Texture2D back=interface->textures[UI_BACK];
    DrawTexturePro(back,(Rectangle){0,0,back.width,back.height},
        (Rectangle){left,top,600*s,340*s},(Vector2){0,0},0,(Color){255,255,255,225});
    text(interface,"Soldat 3D servers",left+16*s,top+12*s,18*s,0,WHITE);
    text(interface,"Server",left+16*s,top+48*s,12*s,0,WHITE);
    text(interface,"Map / mode",left+260*s,top+48*s,12*s,0,WHITE);
    text(interface,"Players",left+505*s,top+48*s,12*s,1,WHITE);
    text(interface,"Ping",left+580*s,top+48*s,12*s,1,WHITE);
    int count=(int)lobby_count(lobby),visible=10;
    if (presses->keys[KEY_DOWN] && *selected+1<count) ++*selected;
    if (presses->keys[KEY_UP] && *selected>0) --*selected;
    if (presses->keys[KEY_PAGE_DOWN] && count) *selected=(*selected+visible<count)?*selected+visible:count-1;
    if (presses->keys[KEY_PAGE_UP]) *selected=*selected>visible ? *selected-visible:0;
    if (count && (*selected<0 || *selected>=count)) *selected=0;
    int first=*selected>=0 ? (*selected/visible)*visible:0;
    Vector2 mouse=GetMousePosition();
    int join=-1;
    for (int row=0;row<visible && first+row<count;++row) {
        int index=first+row;
        const LobbyEntry *server=lobby_entry(lobby,(size_t)index);
        float y=top+(72+row*24)*s;
        Rectangle target={left+10*s,y,580*s,24*s};
        if (CheckCollisionPointRec(mouse,target) && (presses->mouse&(1u<<MOUSE_BUTTON_LEFT))) {
            if (*selected==index) join=index;
            *selected=index;
        }
        Color color=index==*selected ? (Color){88,255,90,255}:WHITE;
        if (server->version!=NETWORK_PROTOCOL_VERSION || server->players>=server->max_players) color=GRAY;
        float name_width=MeasureTextEx(interface->font,server->name,10*s,0).x;
        text(interface,server->name,left+16*s,y+2*s,10*s*fminf(1,230*s/name_width),0,color);
        const char *map_mode=TextFormat("%s / %s",server->map,game_mode_ids[server->mode]);
        float map_width=MeasureTextEx(interface->font,map_mode,10*s,0).x;
        text(interface,map_mode,left+260*s,y+2*s,10*s*fminf(1,180*s/map_width),0,color);
        text(interface,TextFormat("%u/%u +%u",server->players,server->max_players,server->bots),left+505*s,y+2*s,10*s,1,color);
        text(interface,server->ping_ms>=0 ? TextFormat("%.0f ms",server->ping_ms):"...",left+580*s,y+2*s,10*s,1,color);
    }
    if (presses->keys[KEY_ENTER] && count) join=*selected;
    if (join>=0) {
        const LobbyEntry *server=lobby_entry(lobby,(size_t)join);
        if (server->version!=NETWORK_PROTOCOL_VERSION || server->players>=server->max_players || server->ping_ms<0) join=-1;
    }
    const char *status=lobby_status(lobby)==LOBBY_LOADING ? "Discovering servers..." :
        lobby_status(lobby)==LOBBY_UNREACHABLE ? "Directory unreachable. LAN discovery remains available." :
        count==0 ? "No servers found. Host a match or connect to a directory." : TextFormat("%d servers   %d-%d",count,first+1,first+visible<count?first+visible:count);
    text(interface,status,left+16*s,top+318*s,10*s,0,WHITE);
    text(interface,"[F5] Refresh   [F6] Quick join   [Enter] Join   [Esc] Back",left+16*s,top+350*s,11*s,0,WHITE);
    sprite(interface->textures[UI_MENU_CURSOR],mouse.x,mouse.y,s/interface->asset_scale,WHITE);
    return join;
}
