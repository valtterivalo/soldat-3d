#include "snapshot.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check(int condition,const char *message) {
    if (condition) return;
    fprintf(stderr,"snapshot: %s\n",message);
    exit(EXIT_FAILURE);
}

int main(void) {
    unsigned char bytes[513];
    Snapshot restored={0};
    for (unsigned pattern=0;pattern<4;++pattern) {
        for (size_t i=0;i<sizeof(bytes);++i) {
            if (pattern==0) bytes[i]=0;
            if (pattern==1) bytes[i]=(unsigned char)(1+i%255);
            if (pattern==2) bytes[i]=(unsigned char)(i%2);
            if (pattern==3) bytes[i]=i%131<127 ? 0 : (unsigned char)i;
        }
        for (size_t size=0;size<=sizeof(bytes);++size) {
            Snapshot source={bytes,size};
            Snapshot packed=snapshot_pack(&source);
            check(packed.size<=size+5,"packing never expands beyond its fixed header");
            check(snapshot_unpack(&restored,packed.data,packed.size),"small compressed and uncompressed inputs unpack");
            check(restored.size==size && (!size || !memcmp(restored.data,bytes,size)),
                "zeroes, incompressible bytes and mixed data survive byte-exactly");
            free(packed.data);
        }
    }
    unsigned char baseline_bytes[513];
    for (size_t i=0;i<sizeof(baseline_bytes);++i) baseline_bytes[i]=(unsigned char)(i*73);
    const size_t lengths[]={0,1,127,128,129,512,513};
    for (size_t b=0;b<sizeof(lengths)/sizeof(*lengths);++b) {
        Snapshot base={baseline_bytes,lengths[b]};
        for (size_t s=0;s<sizeof(lengths)/sizeof(*lengths);++s) {
            Snapshot source={bytes,lengths[s]};
            Snapshot packed=snapshot_delta_pack(&source,&base);
            check(snapshot_delta_unpack(&restored,&base,packed.data,packed.size),
                "acknowledged baseline deltas unpack across growth and shrinkage");
            check(restored.size==source.size && (!source.size || !memcmp(restored.data,source.data,source.size)),
                "delta decoding restores identical bytes with empty, shorter or longer baselines");
            free(packed.data);
        }
    }
    Snapshot source={baseline_bytes,sizeof(baseline_bytes)};
    Snapshot delta=snapshot_delta_pack(&source,&restored);
    check(snapshot_delta_unpack(&restored,&restored,delta.data,delta.size),"owned baseline can update in place");
    check(restored.size==source.size && !memcmp(restored.data,source.data,source.size),
        "in-place delta applies before replacing its baseline allocation");
    free(delta.data);
    free(restored.data);restored=(Snapshot){malloc(1),1};
    if (!restored.data) abort();
    restored.data[0]=77;
    unsigned char *original=restored.data;
    const unsigned char malformed[][8]={
        {2,0,0,0,0,0,0,0},
        {0,0,0,0,1,0,0,0},
        {1,0,0,0,5,127,0,0},
        {1,0,0,0,1,0xff,0,0},
        {1,0,0,0,127,0x81,0,0},
        {1,255,255,255,255,0xff,0,0}
    };
    const size_t malformed_sizes[]={5,5,7,6,6,6};
    for (size_t i=0;i<sizeof(malformed)/sizeof(*malformed);++i) {
        check(!snapshot_unpack(&restored,malformed[i],malformed_sizes[i]),
            "unknown method, truncated literals and inconsistent output sizes are rejected");
        check(restored.data==original && restored.size==1 && restored.data[0]==77,
            "malformed packed state leaves owned output unchanged");
        check(!snapshot_delta_unpack(&restored,&restored,malformed[i],malformed_sizes[i]),
            "malformed delta rejects atomically even when output owns its baseline");
        check(restored.data==original && restored.size==1 && restored.data[0]==77,
            "malformed delta cannot destroy its acknowledged baseline");
    }

    Game game={.tick=UINT64_C(0x123456789abcdef0),.next_projectile_id=UINT64_C(0xabcdef0123456789),
        .next_event_id=UINT64_C(0xfedcba9876543210),
        .random=123,.mode=MODE_TEAMMATCH};
    for (int i=0;i<ACTOR_COUNT;++i) {
        game.actors[i].life=INACTIVE;
        game.actors[i].spawn_id=(uint32_t)i+900;
        game.actors[i].motion_tick=UINT64_C(0xfedcba9876543210)+(uint64_t)i;
        game.actors[i].shot_sequence=UINT64_C(0xabcdefff12345678)+(uint64_t)i;
    }
    Actor *actor=&game.actors[0];
    actor->life=DEAD;actor->team=TEAM_ALPHA;actor->grenade_weapon=FRAGGRENADE;
    actor->health=-180;actor->position=v3(-0.0f,1.25f,100000.125f);
    actor->slots[0]=(WeaponState){.id=BARRETT,.ammo=7,.startup_count=19};
    actor->loadout[0]=BARRETT;actor->loadout[1]=COLT;
    actor->ragdoll.severed=1u<<19;
    for (int i=0;i<=RAGDOLL_PART_COUNT;++i) {
        actor->ragdoll.position[i]=v3(i*.1f,i*.3f,i*.7f);
        actor->ragdoll.previous[i]=actor->ragdoll.position[i];
    }
    game.projectile_count=game.projectile_capacity=1;
    game.projectiles=malloc(sizeof(*game.projectiles));
    if (!game.projectiles) abort();
    game.projectiles[0]=(Projectile){.weapon=BARRETT,.owner=0,.ticks=100,
        .position={.25f,120.5f,-20.75f},.velocity={1.5f,2.25f,55},
        .id=UINT64_C(0xfedcba9876543210),
        .rewind={REWIND_RENDERED,game.tick-8,game.tick-4,game.tick-1,.375f}};
    Snapshot canonical=snapshot_encode(&game),packed=snapshot_pack(&canonical);
    Snapshot identical=snapshot_delta_pack(&canonical,&canonical);
    check(identical.size<packed.size,"acknowledged identical state compresses using its baseline dictionary");
    Snapshot wrong={malloc(canonical.size),canonical.size};
    if (!wrong.data) abort();
    memcpy(wrong.data,canonical.data,canonical.size);
    wrong.data[wrong.size-1]^=1;
    check(!snapshot_delta_unpack(&restored,&wrong,identical.data,identical.size),
        "a different baseline cannot decode a dictionary-dependent packet");
    check(!snapshot_unpack(&restored,identical.data,identical.size),
        "a dictionary-dependent packet cannot be mistaken for a standalone recovery snapshot");
    check(restored.data==original && restored.size==1 && restored.data[0]==77,
        "missing or mismatched baseline leaves the prior decoded state intact");
    free(wrong.data);free(identical.data);
    for (size_t size=0;size<packed.size;++size) {
        check(!snapshot_unpack(&restored,packed.data,size),"every truncated packed snapshot is rejected");
        check(restored.data==original && restored.size==1 && restored.data[0]==77,
            "truncated snapshots cannot partially replace owned output");
    }
    check(packed.data[0]==1,"game fixture exercises checksummed compression");
    packed.data[packed.size-1]^=1;
    check(!snapshot_unpack(&restored,packed.data,packed.size),"corrupt compressed frame checksum is rejected");
    check(restored.data==original && restored.size==1 && restored.data[0]==77,
        "corruption cannot partially replace the last confirmed state");
    packed.data[packed.size-1]^=1;
    check(snapshot_unpack(&restored,packed.data,packed.size),"packed game state unpacks");
    check(restored.size==canonical.size && !memcmp(restored.data,canonical.data,canonical.size),
        "transport packing leaves canonical serialization unchanged");
    Game replica={0};
    restored.data[7]^=1;
    check(!snapshot_decode(&replica,restored.data,restored.size),"unsupported canonical version rejects before decoding state");
    restored.data[7]^=1;
    check(snapshot_decode(&replica,restored.data,restored.size),"unpacked canonical state decodes");
    check(replica.actors[0].spawn_id==game.actors[0].spawn_id &&
        replica.actors[31].spawn_id==game.actors[31].spawn_id &&
        replica.actors[31].motion_tick==game.actors[31].motion_tick &&
        replica.actors[31].shot_sequence==game.actors[31].shot_sequence &&
        replica.projectiles[0].id==game.projectiles[0].id && replica.projectiles[0].rewind.mode==REWIND_RENDERED &&
        replica.projectiles[0].rewind.before_tick==game.tick-8 &&
        replica.projectiles[0].rewind.after_tick==game.tick-4 &&
        replica.projectiles[0].rewind.applied_tick==game.tick-1 &&
        replica.projectiles[0].rewind.fraction==.375f &&
        replica.next_projectile_id==game.next_projectile_id && replica.next_event_id==game.next_event_id && replica.history==NULL,
        "spawn generations, motion timestamps and projectile identities round-trip without server history");
    Snapshot copy=snapshot_encode(&replica);
    check(copy.size==canonical.size && !memcmp(copy.data,canonical.data,copy.size),
        "decoded game re-encodes to identical canonical bytes");
    free(copy.data);free(packed.data);free(canonical.data);free(restored.data);
    game_free(&replica);game_free(&game);
    puts("Snapshots: lossless packing, acknowledged dictionaries, atomic rejection and canonical identity passed");
}
