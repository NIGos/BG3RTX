// Offline asset database, as the host exposes it to the core (docs/ASSET_DB.md).
//
// The database is built from the user's game install by `python -m tools.assets build`
// (data/assetdb/bg3rtx_assetdb.bin). The host loads it lazily, on the first call that
// needs it, and works out which level is loaded. Plain C structs only (see host_api.h).
//
// Coordinates are absolute world space, the space of the level files. The lighting
// pass works camera-relative: absolute = relative + the camera position (set 0 b2 _m6,
// std140 offset 208, of the G-buffer and lighting passes).
//
// Versioning: this table has its own version. Fields are only ever appended; check
// `version` (or `struct_size`) before using anything newer than version 1.
//   2: runtime lights (the Script Extender bridge merged with the placed lights,
//      docs/SE_BRIDGE.md), se_info, se_environment.
#pragma once
#include <cstddef>
#include <cstdint>

#define BG3RTX_ASSETDB_API_VERSION 2

// ---- lights (record layout of the file's LGHT section, 128 bytes) ----
enum Bg3DbLightType : uint8_t { BG3_DB_LIGHT_POINT = 0, BG3_DB_LIGHT_SPOT = 1, BG3_DB_LIGHT_BOX = 2 };
enum Bg3DbLightFlags : uint32_t {
  BG3_DB_LIGHT_ENABLED = 1u << 0,          // Enabled (story/layers can still toggle it)
  BG3_DB_LIGHT_USE_TEMPERATURE = 1u << 1,  // colour from Kelvin
  BG3_DB_LIGHT_SHADOW = 1u << 2,           // Shadow: casts shadows
  BG3_DB_LIGHT_VOLUMETRIC_SHADOW = 1u << 3,
  BG3_DB_LIGHT_FLICKER = 1u << 4,          // IsFlickering (flicker_speed / flicker_amount)
  BG3_DB_LIGHT_MOVING = 1u << 5,           // IsMoving (move_speed / move_amount): jitters around pos
  BG3_DB_LIGHT_SUNLIGHT = 1u << 6,
  BG3_DB_LIGHT_PRE_EXPOSE = 1u << 7,
  BG3_DB_LIGHT_FLAT_FALLOFF = 1u << 8,
  BG3_DB_LIGHT_CAST_SHADOW = 1u << 9,      // template CastShadow
  BG3_DB_LIGHT_COOKIE = 1u << 10,          // `cookie` names a texture
  BG3_DB_LIGHT_COMBINED = 1u << 11,        // from CombinedLights/
  BG3_DB_LIGHT_NOT_STARTING_LOADED = 1u << 12,  // its sub-level is loaded by the story later
  BG3_DB_LIGHT_HALF_LIT = 1u << 13,
};
struct Bg3DbLight {
  float pos[3];
  float range;             // point/spot: Radius; box: half diagonal + attenuation end
  float rot[4];            // quaternion x, y, z, w; the light looks along local +Z
  float color[3];          // Color as authored (0-1)
  float intensity;         // Intensity (artistic units; the runtime array is a calibrated scale of it)
  float kelvin, gain, scattering;
  float spot_inner_deg, spot_outer_deg;   // Angle (x, y)
  float box[3];            // DirectionLightDimensions (box lights)
  float att_start, att_end, att_side;     // DirectionLightAttenuation*
  float flicker_speed, flicker_amount, move_speed, move_amount;
  uint32_t flags;          // Bg3DbLightFlags
  uint8_t type;            // Bg3DbLightType
  uint8_t att_function, channel, pad;
  uint32_t instance;       // sub-level instance within the level
  uint32_t name;           // string refs: Bg3AssetDbApi::string
  uint32_t cookie;
};
static_assert(sizeof(Bg3DbLight) == 128, "Bg3DbLight must match the LGHT record");

// ---- materials ----
// Roles, in the order of tools/assets/extract/materials.py ROLES.
enum Bg3MatRole : uint32_t {
  BG3_MAT_OTHER, BG3_MAT_BASE_COLOR_MAP, BG3_MAT_NORMAL_MAP, BG3_MAT_PHYSICAL_MAP, BG3_MAT_EMISSIVE_MAP,
  BG3_MAT_MASK_MAP, BG3_MAT_BASE_COLOR, BG3_MAT_EMISSIVE_COLOR, BG3_MAT_EMISSIVE_INTENSITY, BG3_MAT_ROUGHNESS,
  BG3_MAT_METALNESS, BG3_MAT_REFLECTANCE, BG3_MAT_ALPHA_CUTOFF, BG3_MAT_OPACITY, BG3_MAT_VT_TABLE,
  BG3_MAT_VT_ALBEDO, BG3_MAT_VT_NORMAL, BG3_MAT_VT_PHYSICAL, BG3_MAT_VT_TILESET_INDEX, BG3_MAT_VT_UV_TRANSFORM,
  BG3_MAT_DIFFUSION_PROFILE_INDEX, BG3_MAT_LIGHT_CHANNEL, BG3_MAT_FADE_OPACITY, BG3_MAT_DETAIL_NORMAL_MAP,
  BG3_MAT_SAMPLER, BG3_MAT_ROLE_COUNT
};
// Where a parameter lives. kind 0 = the material has no parameter with this role.
enum Bg3MatKind : uint8_t {
  BG3_MAT_KIND_NONE, BG3_MAT_KIND_SCALAR, BG3_MAT_KIND_VEC2, BG3_MAT_KIND_VEC3, BG3_MAT_KIND_VEC4,
  BG3_MAT_KIND_TEXTURE, BG3_MAT_KIND_SAMPLER, BG3_MAT_KIND_VT_TABLE, BG3_MAT_KIND_VT_CACHE, BG3_MAT_KIND_ENGINE_CB
};
// Primary class (tools/assets/extract/materials.py CLASSES).
enum Bg3MatClass : uint32_t {
  BG3_MAT_CLASS_OPAQUE, BG3_MAT_CLASS_ALPHA_TEST, BG3_MAT_CLASS_EMISSIVE, BG3_MAT_CLASS_REFRACTIVE,
  BG3_MAT_CLASS_SSS_SKIN, BG3_MAT_CLASS_HAIR, BG3_MAT_CLASS_FOLIAGE, BG3_MAT_CLASS_TRANSPARENT,
  BG3_MAT_CLASS_UNLIT, BG3_MAT_CLASS_CLOTH, BG3_MAT_CLASS_ANISOTROPIC, BG3_MAT_CLASS_WATER, BG3_MAT_CLASS_DECAL
};
enum Bg3MatFlags : uint32_t {
  BG3_MAT_F_OPAQUE = 1u << 0, BG3_MAT_F_ALPHA_TEST = 1u << 1, BG3_MAT_F_ALPHA_BLEND = 1u << 2,
  BG3_MAT_F_ADDITIVE = 1u << 3, BG3_MAT_F_EMISSIVE = 1u << 4, BG3_MAT_F_REFRACTIVE = 1u << 5,
  BG3_MAT_F_SSS = 1u << 6, BG3_MAT_F_HAIR = 1u << 7, BG3_MAT_F_FOLIAGE = 1u << 8, BG3_MAT_F_CLOTH = 1u << 9,
  BG3_MAT_F_ANISOTROPIC = 1u << 10, BG3_MAT_F_UNLIT = 1u << 11, BG3_MAT_F_WATER = 1u << 12,
  BG3_MAT_F_DECAL = 1u << 13, BG3_MAT_F_VIRTUAL_TEXTURE = 1u << 14, BG3_MAT_F_TWO_SIDED = 1u << 15,
  BG3_MAT_F_WORLD_POSITION_OFFSET = 1u << 16, BG3_MAT_F_VERTEX_COLOR = 1u << 17,
  BG3_MAT_F_LUMINANCE_EMISSIVE = 1u << 18, BG3_MAT_F_CHARACTER = 1u << 19, BG3_MAT_F_VFX = 1u << 20,
  BG3_MAT_F_FORWARD_LIT = 1u << 21, BG3_MAT_F_SCENE_COLOR = 1u << 22, BG3_MAT_F_DISCARDS = 1u << 23,
};
struct Bg3MatSlot {
  uint8_t kind;            // Bg3MatKind
  uint8_t set, binding;    // Vulkan descriptor set and binding (constant buffer for the CB kinds)
  uint8_t layer;           // VT cache layer (0 albedo, 1 normal, 2 physical)
  uint16_t offset;         // byte offset in the constant buffer (CB kinds), 0xFFFF otherwise
  uint16_t pad;
  float value[4];          // the template's default value (CB kinds)
  const char* name;        // the template's ParameterName
};
struct Bg3MaterialInfo {
  const char* template_name;   // below Assets/Materials, e.g. "Base/Base_VT"
  uint32_t cls;                // Bg3MatClass
  uint32_t flags;              // Bg3MatFlags
  uint32_t shading_model;      // = the G-buffer material class (FRAME_MAP): 0 standard ... 6 cloth
  uint32_t stage;              // 0 vertex, 4 fragment
  uint32_t pass;               // 0 DEF, 1 FOR, 2 EMI, 3 DEP, 4 DEPS, 5 DEPST, 6 VEL, 7 BAKE
  uint32_t instanced;          // 1: the StaticInstanced variant
  uint32_t aliases;            // other templates compiled to the identical shader (their bindings agree)
  uint8_t engine_cb_set, engine_cb_binding, material_cb_set, material_cb_binding;
  uint32_t engine_cb_size, material_cb_size;
  Bg3MatSlot slot[BG3_MAT_ROLE_COUNT];   // the first parameter of each role
};

// ---- rooms, roofs, lighting ----
enum Bg3RoomFlags : uint32_t {
  BG3_ROOM_INSIDE = 1u << 0,        // inside a RoomTrigger
  BG3_ROOM_ROOFED = 1u << 1,        // ... whose roof the game cuts away (RenderMethod 1) or a roof floor lists
  BG3_ROOM_COVERED_FLOOR = 1u << 2, // inside a FloorTrigger of a building with a higher floor
  BG3_ROOM_PORTAL = 1u << 3,        // inside a PortalTrigger
};
struct Bg3RoomInfo {
  uint32_t flags;          // Bg3RoomFlags
  uint32_t room;           // trigger index of the room (UINT32_MAX if none)
  int32_t floor;           // FloorTrigger.Floor, when inside one
  const char* room_name;
  const char* building;    // FloorTrigger.BuildingUUID
  uint32_t lighting;       // preset index: the TriggerLighting containing the point, else the level's
  uint32_t atmosphere;     // same for TriggerAtmosphere
};
struct Bg3DbPreset {       // LightingBank / AtmosphereBank resource (PRES record, 64 bytes)
  uint32_t id, name, kind, flags;     // kind 0 lighting, 1 atmosphere; flags bit 0 sun shadows
  float sun_color[3], sun_intensity;
  float sun_yaw_deg, sun_pitch_deg, sun_size_deg, sun_kelvin;
  float sky_color[3], sky_intensity;
};
static_assert(sizeof(Bg3DbPreset) == 64, "Bg3DbPreset must match the PRES record");
struct Bg3DbProbe {        // PRBE record, 64 bytes
  float pos[3], radius;
  float rot[4];
  float size[3], transition;
  float intensity;
  uint32_t flags;          // bit 0 enabled, bits 8-15 LightProbeShape
  uint32_t first_capture, capture_count;
};
struct Bg3DbProbeCapture { // PCAP record, 80 bytes: one bake per lighting preset
  uint32_t preset;         // index into presets (UINT32_MAX: not in the database)
  uint32_t path;           // string: "<...>/LightProbes/<probe>_<lighting>" + _Diffuse_RGB.dds / _Specular_RGB.dds / _SH.dat
  uint32_t lighting_id;    // string: the lighting resource UUID
  uint32_t has_sh;
  float sh[16];            // _SH.dat: L1 SH, 4 x float4
};
static_assert(sizeof(Bg3DbProbe) == 64 && sizeof(Bg3DbProbeCapture) == 80, "probe records");

// ---- runtime lights (version 2, docs/SE_BRIDGE.md) ----
// The lights the game has now: the Script Extender bridge's export (every light entity
// near the controlled character: placed lights, item and scenery-visual lights, the
// character lights) when it is running, merged with the placed lights of the database.
// Without the bridge, the database's placed lights alone.
enum Bg3RtLightOwner : uint8_t {
  BG3_RTL_OWNER_UNKNOWN = 0,    // a visual's light whose owner was not found (treated as scenery)
  BG3_RTL_OWNER_PLACED = 1,     // a placed level light (Lights/, CombinedLights/)
  BG3_RTL_OWNER_ITEM = 2,       // a light of an item's visual (lamps, torches, braziers as items)
  BG3_RTL_OWNER_SCENERY = 3,    // a light of a scenery visual
  BG3_RTL_OWNER_CHARACTER = 4,  // character lights: the darkvision / active character light, rim and fill
};
enum Bg3RtLightFlags : uint32_t {
  BG3_RTL_ENABLED = 1u << 0,
  BG3_RTL_USE_TEMPERATURE = 1u << 1,
  BG3_RTL_FILL = 1u << 2,           // LightFlags FillLight (bridge only)
  BG3_RTL_FLICKER = 1u << 3,        // IsFlickering
  BG3_RTL_MOVING = 1u << 4,         // IsMoving (jitters by move_amount)
  BG3_RTL_SHADOW = 1u << 5,         // template Shadow: the game renders a shadow map for it
  BG3_RTL_CAST_SHADOW = 1u << 6,    // template CastShadow
  BG3_RTL_VOLUMETRIC_SHADOW = 1u << 7,
  BG3_RTL_PLAYER_LIGHT = 1u << 8,   // Light_Player_* template (darkvision / active character light)
  BG3_RTL_FROM_SE = 1u << 16,       // the bridge reported it (state is live)
  BG3_RTL_FROM_DB = 1u << 17,       // the database has it (db_index is valid)
};
struct Bg3RtLight {      // 80 bytes
  float pos[3];          // absolute world space
  float radius;          // Radius (the range the lighting uses)
  float color[3];        // Color as authored (0-1)
  float intensity;
  float kelvin;
  float source_radius;   // emitter radius for soft shadows (m), from the fixture class (docs/SE_BRIDGE.md)
  float flicker_speed, flicker_amount, move_speed, move_amount;
  uint32_t flags;        // Bg3RtLightFlags
  uint8_t type;          // Bg3DbLightType (0 point, 1 spot, 2 box)
  uint8_t owner;         // Bg3RtLightOwner
  uint8_t channel, pad;  // LightChannelFlag (low 8 bits)
  uint32_t db_index;     // database light (UINT32_MAX: none)
  const char* name;      // template name ("" if unknown); valid for the process lifetime
};
static_assert(sizeof(Bg3RtLight) == 80, "Bg3RtLight is 80 bytes");
struct Bg3SeInfo {
  uint32_t active;       // 1: a fresh bridge export (this game session, younger than se_stale_ms)
  uint32_t seq;          // export sequence number
  float age_s;           // age of the export
  float host[3];         // the controlled character's position
  const char* level;     // the controlled character's level (bridge)
  const char* region;    // Osiris region (bridge, server context)
  uint32_t lights_all;   // light entities the client has (all loaded levels)
  uint32_t lights_out;   // exported (within 60 m of the character)
  float lua_avg_us, lua_max_us;   // the export's Lua cost per period (4 Hz)
  float lua_classify_us;          // one-off classification, total so far
  uint32_t exports_read; // exports the layer has parsed
};

struct Bg3AssetDbApi {
  uint32_t version;        // BG3RTX_ASSETDB_API_VERSION
  uint32_t struct_size;

  // Loads the database if it is not loaded yet. False when it is missing or invalid
  // (the log says why once); every query below then returns nothing.
  bool (*available)();
  // The level (world) the game has loaded, as the database names it ("CTY_Main_A"), and
  // its index; -1 while unknown. `source`: "save" (the save the game was started with),
  // "lights" (runtime light positions matched the database) or "none".
  int32_t (*current_level)(const char** name, const char** source);
  // Lights of the current level whose position is within `radius` of `pos`, nearest
  // first. Writes up to `max` global light indices; returns how many matched in total.
  uint32_t (*lights_near)(const float pos[3], float radius, uint32_t* out, uint32_t max);
  // Lights of the current level whose reach (pos, range) overlaps the box.
  uint32_t (*lights_in_box)(const float bmin[3], const float bmax[3], uint32_t* out, uint32_t max);
  const Bg3DbLight* (*light)(uint32_t index);
  // The material of a shader module, by the layer's SPIR-V hash (vertex or fragment).
  bool (*material)(uint64_t shader_hash, Bg3MaterialInfo* out);
  // Engine (non-material) shaders: their file name, e.g. "ClusteredLighting/ClusteredLighting_0".
  const char* (*engine_shader)(uint64_t shader_hash);
  // Rooms and roofs at a point of the current level; returns `out->flags`.
  uint32_t (*room_at)(const float pos[3], Bg3RoomInfo* out);
  const Bg3DbPreset* (*preset)(uint32_t index);
  uint32_t (*probes_near)(const float pos[3], float radius, uint32_t* out, uint32_t max);
  const Bg3DbProbe* (*probe)(uint32_t index);
  const Bg3DbProbeCapture* (*probe_capture)(uint32_t index);
  const char* (*string)(uint32_t ref);

  // ---- version 2 ----
  // Runtime lights within `radius` of `pos`, nearest first: the bridge's lights (when it
  // is active) and the database's placed lights it did not report. Returns the total;
  // writes up to `max`.
  uint32_t (*runtime_lights)(const float pos[3], float radius, Bg3RtLight* out, uint32_t max);
  // The bridge's state; false (and out->active = 0) when it is not running.
  bool (*se_info)(Bg3SeInfo* out);
  // The atmosphere and lighting resource (UUID) in force at `pos`: the database's trigger
  // containing it, with the resource the bridge says story scripting set on that trigger
  // (else the trigger's own). False when neither is known. BG3 has no time of day.
  bool (*se_environment)(const float pos[3], const char** atmosphere, const char** lighting);
};
