#ifndef GMODC_LUA_TYPES_H
#define GMODC_LUA_TYPES_H

#ifdef ENTITY
#undef ENTITY
#endif

#ifdef VECTOR
#undef VECTOR
#endif

#define GMODC_TYPES(_)                                                                            \
  /* Lua Types */                                                                                 \
  _(NIL) _(BOOL) _(LIGHTUSERDATA) _(NUMBER) _(STRING) _(TABLE) _(FUNCTION) _(USERDATA) _(THREAD)  \
  /* Gmod Types */                                                                                \
  _(ENTITY) _(VECTOR) _(ANGLE) _(PHYSOBJ) _(SAVE) _(RESTORE) _(DAMAGEINFO) _(EFFECTDATA)          \
  _(MOVEDATA) _(RECIPIENTFILTER) _(USERCMD) _(SCRIPTEDVEHICLE) _(MATERIAL) _(PANEL)               \
  _(PARTICLE) _(PARTICLEEMITTER) _(TEXTURE) _(USERMSG) _(CONVAR) _(IMESH) _(MATRIX)               \
  _(SOUND) _(PIXELVISHANDLE) _(DLIGHT) _(VIDEO) _(FILE) _(LOCOMOTION) _(PATH) _(NAVAREA)          \
  _(SOUNDHANDLE) _(NAVLADDER) _(PARTICLESYSTEM) _(PROJECTEDTEXTURE) _(PHYSCOLLIDE) _(COUNT)
  
enum {
  NONE = -1,
#define _TDEF(name) TYPE_##name,
  GMODC_TYPES(_TDEF)
#undef _TDEF
  LAST
};

#endif
