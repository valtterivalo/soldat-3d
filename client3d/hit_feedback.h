#ifndef SOLDAT3D_HIT_FEEDBACK_H
#define SOLDAT3D_HIT_FEEDBACK_H

#include "game.h"

typedef enum { HIT_NONE, HIT_DAMAGE, HIT_KILL } HitConfirmation;
typedef struct { HitConfirmation kind; float age; } HitFeedback;

static inline HitConfirmation hit_confirmation(const GameEvent *events, size_t count, int local_actor) {
    HitConfirmation result=HIT_NONE;
    for (size_t i=0;i<count;++i) {
        const GameEvent *event=&events[i];
        if (event->actor!=local_actor || event->target==local_actor) continue;
        if (event->kind==EVENT_KILL) return HIT_KILL;
        if (event->kind==EVENT_HIT) result=HIT_DAMAGE;
    }
    return result;
}

#endif
