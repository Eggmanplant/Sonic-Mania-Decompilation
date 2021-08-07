#ifndef OBJ_SPARKRAIL_H
#define OBJ_SPARKRAIL_H

#include "SonicMania.h"

// Object Class
typedef struct {
    RSDK_OBJECT
    ushort aniFrames;
    ushort sfxPon;
} ObjectSparkRail;

// Entity Class
typedef struct {
    RSDK_ENTITY
    Vector2 size;
    Hitbox hitbox;
} EntitySparkRail;

// Object Struct
extern ObjectSparkRail *SparkRail;

// Standard Entity Events
void SparkRail_Update(void);
void SparkRail_LateUpdate(void);
void SparkRail_StaticUpdate(void);
void SparkRail_Draw(void);
void SparkRail_Create(void* data);
void SparkRail_StageLoad(void);
void SparkRail_EditorDraw(void);
void SparkRail_EditorLoad(void);
void SparkRail_Serialize(void);

// Extra Entity Functions


#endif //!OBJ_SPARKRAIL_H