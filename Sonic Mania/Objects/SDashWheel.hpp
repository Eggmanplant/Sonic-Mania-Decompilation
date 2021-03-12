#ifndef OBJ_SDASHWHEEL_H
#define OBJ_SDASHWHEEL_H

#include "../SonicMania.hpp"

// Object Class
struct ObjectSDashWheel : Object {
    ushort value1;
    colour value2;
    int value3[33]; //= { 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 3, 3, 4, 4, 5, 6, 6, 7, 8, 9, 10, 11, 12, 14, 15, 17, 19, 22, 26 };
    ushort value4;
    ushort value5;
};

// Entity Class
struct EntitySDashWheel : Entity {

};

// Object Struct
extern ObjectSDashWheel *SDashWheel;

// Standard Entity Events
void SDashWheel_Update();
void SDashWheel_LateUpdate();
void SDashWheel_StaticUpdate();
void SDashWheel_Draw();
void SDashWheel_Create(void* data);
void SDashWheel_StageLoad();
void SDashWheel_EditorDraw();
void SDashWheel_EditorLoad();
void SDashWheel_Serialize();

// Extra Entity Functions


#endif //!OBJ_SDASHWHEEL_H