#ifndef OBJ_LRZ1INTRO_H
#define OBJ_LRZ1INTRO_H

#include "../SonicMania.hpp"

#if RETRO_USE_PLUS
// Object Class
struct ObjectLRZ1Intro : Object {

};

// Entity Class
struct EntityLRZ1Intro : Entity {

};

// Object Struct
extern ObjectLRZ1Intro LRZ1Intro;

// Standard Entity Events
void LRZ1Intro_Update();
void LRZ1Intro_LateUpdate();
void LRZ1Intro_StaticUpdate();
void LRZ1Intro_Draw();
void LRZ1Intro_Create(void* data);
void LRZ1Intro_StageLoad();
void LRZ1Intro_EditorDraw();
void LRZ1Intro_EditorLoad();
void LRZ1Intro_Serialize();

// Extra Entity Functions
#endif

#endif //!OBJ_LRZ1INTRO_H