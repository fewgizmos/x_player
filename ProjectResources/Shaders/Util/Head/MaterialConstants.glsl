#ifndef X_MATERIAL_CONSTANTS
#define X_MATERIAL_CONSTANTS

const uint NoTexture = 0xFFFFFFFFu;

const uint AlphaModeOpaque = 0u;
const uint AlphaModeMask   = 1u;
const uint AlphaModeBlend  = 2u;

const uint MaterialTexturesCount = 14u;

const uint TextureSlotBaseColor         = 0u;
const uint TextureSlotMetallicRoughness = 1u;
const uint TextureSlotNormal            = 2u;
const uint TextureSlotOcclusion         = 3u;
const uint TextureSlotEmissive          = 4u;
const uint TextureSlotSpecular          = 5u;
const uint TextureSlotSpecularColor     = 6u;
const uint TextureSlotDiffuseTransmission      = 7u;
const uint TextureSlotDiffuseTransmissionColor = 8u;
const uint TextureSlotClearcoat          = 9u;
const uint TextureSlotClearcoatRoughness = 10u;
const uint TextureSlotClearcoatNormal    = 11u;
const uint TextureSlotSheenColor     = 12u;
const uint TextureSlotSheenRoughness = 13u;

#endif
