//LibraryGBuffer.hlsl

#ifndef LIBRARY_GBUFFER
#define LIBRARY_GBUFFER

//DONT TOUCH THESE IF YOU DON'T KNOW WHAT YOU ARE DOING!!!

//these help tell us what KIND of shading to do for a material
static const int SHADINGMODELID_UNLIT              = 0;
static const int SHADINGMODELID_DEFAULT_LIT        = 1;
//static const int SHADINGMODELID_SUBSURFACE       = 2; //UNUSED BY THE GAME
static const int SHADINGMODELID_PREINTEGRATED_SKIN = 3;
//static const int SHADINGMODELID_CLEAR_COAT       = 4; //UNUSED BY THE GAME
static const int SHADINGMODELID_SUBSURFACE_PROFILE = 5;
//static const int SHADINGMODELID_TWOSIDED_FOLIAGE = 6; //UNUSED BY THE GAME
static const int SHADINGMODELID_HAIR               = 7;
static const int SHADINGMODELID_CLOTH              = 8;
static const int SHADINGMODELID_EYE                = 9;
//static const int SHADINGMODELID_NUM              = 10; //UNUSED BY THE GAME
//static const int SHADINGMODELID_MASK             = 0xF; //UNUSED BY THE GAME

#endif //LIBRARY_GBUFFER