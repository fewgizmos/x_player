#ifndef X_MATERIAL_CONSTANTS
#define X_MATERIAL_CONSTANTS

const uint NoTexture = 0xFFFFFFFFu;

const uint AlphaModeOpaque = 0u;
const uint AlphaModeMask   = 1u;
const uint AlphaModeBlend  = 2u;

const uint MaterialTexturesCount = 21u;
const uint MaterialUvPairCount = (MaterialTexturesCount + 1u) / 2u;

const uint MaterialUnlit       = 1u << 0;
const uint MaterialDoubleSided = 1u << 1;
const uint MaterialCastShadows = 1u << 2;

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

const uint TextureSlotHeight = 14u;
const uint TextureSlotTransmission = 15u;
const uint TextureSlotThickness = 16u;
const uint TextureSlotIridescence = 17u;
const uint TextureSlotIridescenceThickness = 18u;
const uint TextureSlotAnisotropy = 19u;
const uint TextureSlotPackedNormalMetallicRoughness = 20u;

#endif
