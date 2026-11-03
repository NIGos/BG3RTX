#pragma once
// S3: shared host layouts let native coverage use one module per entry.
#ifdef RT_SPEC_NATIVE
#if defined(RT_NATIVE_TILESET) || defined(RT_NATIVE_SCALP) || defined(RT_NATIVE_HAIR)
#error RT_SPEC_NATIVE replaces RT_NATIVE_TILESET / RT_NATIVE_SCALP / RT_NATIVE_HAIR
#endif
[[vk::constant_id(1008)]] const uint kSpecTileset = 0u;
[[vk::constant_id(1010)]] const uint kSpecScalpHair = 0u;
#define RT_NATIVE_TILESET 1
#define RT_NATIVE_SCALP 1
#define RT_NATIVE_HAIR 1
#define RT_TILESET_ON (kSpecTileset != 0u)
#define RT_SCALP_ON (kSpecScalpHair != 0u)
#define RT_HAIR_ON (kSpecScalpHair != 0u)
#else
#define RT_TILESET_ON true
#define RT_SCALP_ON true
#define RT_HAIR_ON true
#endif
#ifdef RT_SPEC_HAIR_ORIGIN
#ifdef RT_HAIR_PHYSICAL_ORIGIN
#error RT_SPEC_HAIR_ORIGIN replaces RT_HAIR_PHYSICAL_ORIGIN
#endif
#ifndef RT_SPEC_NATIVE
#error RT_SPEC_HAIR_ORIGIN needs RT_SPEC_NATIVE
#endif
[[vk::constant_id(1009)]] const uint kSpecHairOrigin = 0u;
#define RT_HAIR_PHYSICAL_ORIGIN 1
#define RT_HAIR_ORIGIN_ON (kSpecHairOrigin != 0u)
#else
#define RT_HAIR_ORIGIN_ON true
#endif
