--[[
  gr_init_dx11.lua LIMPIO para PES 2021 1.07.02 (10/11/2026)
  Base: script original de Konami (dt00_x64.cpk) + los cambios de UltraGraphicsConfig v5.1 que el exe SI lee.
  Fuera: 153 claves, 9 plugins, 24 tablas y 2 funciones que este exe no tiene (el motor las ignora).
  Cada clave de las tablas de calidad esta verificada contra el lector del exe (fox::gr::*SettingsParser).
  Con los valores "ugc" la imagen deberia ser la misma que con UltraGraphicsConfig (sin probar en juego).
]]

-- ===================== PERILLAS =====================
local SOMBRA_SOL        = 4096   -- mapa de sombras del sol en calidad Alta. Konami 2048, UGC 8192. 4096 = mitad de costo por lado
local SOMBRA_JUGADORES  = 4096   -- sombra propia de PES (PluginPesShadow) en Alta. Konami 4096, UGC 8192
local SOMBRA_FOCOS      = 4096   -- focos (spot). Konami 2048, UGC 4096
local SOMBRA_PUNTUAL    = 4096   -- luces puntuales. Konami 4096
local CASCADA_ALCANCE   = 2.0    -- alcance de las cascadas de sombra. Konami 1.0, UGC 2.0
local CASCADA_MEZCLA    = 1.0    -- mezcla entre cascadas. Konami 0, UGC 1
local OCLUSION_MOTOR    = false  -- SSAO del motor. Konami false, UGC true. Si usas la oclusion de PES21FX conviene false (doble trabajo)
local SSS_MOTOR         = 0      -- SSS del motor (EnableFilter). Konami 0, UGC 1. Si usas el SSS de PES21FX conviene 0
local SSS_FADE          = 100.0  -- FadeOffset del SSS del motor. Konami 0, UGC 100 (Alta)
local LOD_RESOLUCION    = 1800   -- resolucion de referencia para elegir LOD. Konami 1280, UGC 1800 (mas alto = modelos detallados desde mas lejos)
local DIST_MODELOS      = 1024   -- RejectionLengthBias de modelos y clones en Alta. Konami 64 / 128, UGC 1024
local LUCES_SOMBRA_FIJAS    = 32 -- luces locales fijas con sombra en Alta. Konami 8, UGC 32
local LUCES_SOMBRA_MOVILES  = 16 -- Konami 4, UGC 16
local LUCES_MAX         = 2048   -- luces locales dibujadas en Alta. Konami 524, UGC 2048
local PUBLICO_CALIDAD   = 4      -- QualityType del publico en Alta. Konami 2, UGC 4 (fuera del rango de Konami: efecto sin verificar)
local SAO_CALIDAD       = 7      -- QualityType de SAO en Alta. Konami 2, UGC 7 (idem)
local DOF_CALIDAD       = 4      -- QualityType del desenfoque en Alta. Konami 0, UGC 4 (idem)
local BLUR_BASE         = 0.018  -- BaseAmount del desenfoque de movimiento en Alta. Konami 0, UGC 0.018
-- =====================================================

GrTools:SetLodFactorResolution(LOD_RESOLUCION)
GrTools:DisableTextureStreaming()
if GrTools.SetTexturePackLoadConditioningEnable then GrTools:SetTexturePackLoadConditioningEnable(true) end   -- UGC; la funcion existe en el exe
GrTools.SetDefaultTextureLoadPath("tmp/")
GrTools.SetReflectionTexture("/Assets/fox/effect/gr_pic/default_reflection.ftex")
GrTools.SetMaterialTexture("/Assets/fox/effect/gr_pic/materials_alp_rgba32_nomip_nrt.ftex")
GrTools.SetMaterialParamBinary("/Assets/fox/effect/gr_pic/material_params.fmtt")

-- ===================== PASOS DEL DIBUJO (orden y prioridades de Konami) =====================
GrRenderPlugin.AddPlugin(GrPluginModelSetup{ pluginName = "MODEL_SETUP", priority = 1, isActive = true })
GrRenderPlugin.AddPlugin(GrPluginPrecomputeSky{ pluginName = "PRECOMPUTE_SKY", priority = 7, isActive = true })
GrRenderPlugin.AddPlugin(GrPluginCanvas{ pluginName = "CANVAS_RENDERER", priority = 8 })
GrRenderPlugin.AddPlugin(GrPluginCrowdImpostor{ pluginName = "CROWD_IMPOSTOR", priority = 8 })
GrRenderPlugin.AddPlugin(GrPluginPitchEffect{ pluginName = "PITCH_EFFECT", priority = 8 })
GrRenderPlugin.AddPlugin(GrPluginDeferredRendering{ pluginName = "DEFERRED", priority = 10 })
GrRenderPlugin.AddPlugin(GrPluginDeferredGeometry{ pluginName = "GEOMETRY_PASS", priority = 10, parentPluginName = "DEFERRED" })
GrRenderPlugin.AddPlugin(GrPluginCrowdDepth{ pluginName = "CROWD_DRAW_DEPTH", priority = 4, parentPluginName = "GEOMETRY_PASS" })
GrRenderPlugin.AddPlugin(GrPluginDepthModel{ pluginName = "DEPTH_PREPASS", priority = 5, parentPluginName = "GEOMETRY_PASS" })
GrRenderPlugin.AddPlugin(GrPluginPlanarShadow{ pluginName = "PLANAR_SHADOW", priority = 6, parentPluginName = "GEOMETRY_PASS" })
GrRenderPlugin.AddPlugin(GrPluginCrowdDeferred{ pluginName = "CROWD_PASS", priority = 7, parentPluginName = "GEOMETRY_PASS" })
GrRenderPlugin.AddPlugin(GrPluginDeferredGeometryOpaque{ pluginName = "OPAQUE_PASS", priority = 8, parentPluginName = "GEOMETRY_PASS" })
GrRenderPlugin.AddPlugin(GrPluginDeferredGeometryMasked{ pluginName = "MASK_PASS", priority = 10, parentPluginName = "GEOMETRY_PASS" })
GrRenderPlugin.AddPlugin(GrPluginPrimitiveDeferred{ pluginName = "PRIMITIVES_DEFERRED", priority = 27, parentPluginName = "GEOMETRY_PASS" })
GrRenderPlugin.AddPlugin(GrPluginDeferredGeometryDecal{ pluginName = "DECAL_PASS", priority = 30, parentPluginName = "GEOMETRY_PASS" })
if GrPluginMaterialLayer then GrRenderPlugin.AddPlugin(GrPluginMaterialLayer{ pluginName = "MATERIAL_LAYER", priority = 110, parentPluginName = "GEOMETRY_PASS", isActive = true }) end   -- agregado por UGC; la clase existe en el exe, efecto sin verificar
GrRenderPlugin.AddPlugin(GrPluginPesShadow{ pluginName = "PES_SHADOW", priority = 135, parentPluginName = "GEOMETRY_PASS" })
GrRenderPlugin.AddPlugin(GrPluginShrinkGBuffer{ pluginName = "SHRINK_GBUFFER", priority = 12, parentPluginName = "DEFERRED" })
GrRenderPlugin.AddPlugin(GrPluginAmbientObscuranceSSAO{ pluginName = "AMBIENTOBSCURANCE_SSAO", priority = 18, parentPluginName = "DEFERRED", isActive = OCLUSION_MOTOR })
GrRenderPlugin.AddPlugin(GrPluginDeferredShading{ pluginName = "SHADING_PASS", priority = 20, parentPluginName = "DEFERRED" })
GrRenderPlugin.AddPlugin(GrPluginEnlighten{ pluginName = "ENLIGHTEN", priority = 4, parentPluginName = "SHADING_PASS", isActive = true })
if GrPluginContactShadow then GrRenderPlugin.AddPlugin(GrPluginContactShadow{ pluginName = "CONTACT_SHADOW", priority = 35, parentPluginName = "SHADING_PASS", isActive = true }) end   -- agregado por UGC; la clase existe en el exe, efecto sin verificar
if GrPluginOccluder then GrRenderPlugin.AddPlugin(GrPluginOccluder{ pluginName = "OCCLUDER", priority = 6 }) end   -- agregado por UGC; la clase existe en el exe, efecto sin verificar
GrRenderPlugin.AddPlugin(GrPluginReflectionProbe{ pluginName = "REFLECTION_PROBE", priority = 5, parentPluginName = "SHADING_PASS" })
GrRenderPlugin.AddPlugin(GrPluginLocalReflectionPrepass{ pluginName = "LOCALREFLECTION_TRACE", priority = 6, parentPluginName = "SHADING_PASS" })
GrRenderPlugin.AddPlugin(GrPluginSphericalHarmonics{ pluginName = "PLUGIN_SPHERICALHARMONICS", priority = 10, parentPluginName = "SHADING_PASS", isActive = true })
GrRenderPlugin.AddPlugin(GrPluginLightOccluder{ pluginName = "PLUGIN_LIGHTOCCLUDER", priority = 15, parentPluginName = "SHADING_PASS", isActive = true })
GrRenderPlugin.AddPlugin(GrPluginLocalLight{ pluginName = "LOCAL_LIGHTS", priority = 20, parentPluginName = "SHADING_PASS", isActive = true })
GrRenderPlugin.AddPlugin(GrPluginGlobalLight{ pluginName = "GLOBAL_LIGHTS", priority = 25, parentPluginName = "SHADING_PASS", isActive = true })
GrRenderPlugin.AddPlugin(GrPluginPlanarShadowCombiner{ pluginName = "PLANAR_SHADOW_COMBINER", priority = 27, parentPluginName = "SHADING_PASS" })
GrRenderPlugin.AddPlugin(GrPluginShadow{ pluginName = "SUN_SHADOW", priority = 30, parentPluginName = "SHADING_PASS" })
GrRenderPlugin.AddPlugin(GrPluginSunlight{ pluginName = "SUNLIGHT", priority = 40, parentPluginName = "SHADING_PASS" })
GrRenderPlugin.AddPlugin(GrPluginEnvmapLight{ pluginName = "ENVMAP_LIGHT", priority = 50, parentPluginName = "SHADING_PASS" })
GrRenderPlugin.AddPlugin(GrPluginSubSurfaceScatter{ pluginName = "SUBSURFACE_SCATTER", priority = 60, parentPluginName = "SHADING_PASS", isActive = true })
GrRenderPlugin.AddPlugin(GrPluginSky{ pluginName = "SKY", priority = 20, isActive = true })
GrRenderPlugin.AddPlugin(GrPluginKeyingMatte{ pluginName = "KEYING_MATTE", priority = 21, isActive = true })
GrRenderPlugin.AddPlugin(GrPluginGlobalVolumetricFog{ pluginName = "GLOBAL_VOLUMETRIC_FOG", priority = 8, isActive = true })
GrRenderPlugin.AddPlugin(GrPluginAlphaModel{ pluginName = "ALPHA_MODEL", priority = 28 })
GrRenderPlugin.AddPlugin(GrPluginGoalnet{ pluginName = "GOALNET_PASS", priority = 29 })
GrRenderPlugin.AddPlugin(GrPluginPrimitive{ pluginName = "PRIMITIVES", priority = 30, shrinkLevel = 0, shrinkFilter = "BILATERAL" })
GrRenderPlugin.AddPlugin(GrPluginPrimitivePostPass{ pluginName = "PRIMITIVES_POST_PASS", priority = 31 })
GrRenderPlugin.AddPlugin(GrPluginLocalReflectionResolve{ pluginName = "LOCALREFLECTION_RESOLVE", priority = 32 })
GrRenderPlugin.AddPlugin(GrPluginVelocity{ pluginName = "VELOCITY_PASS", priority = 40 })
GrRenderPlugin.AddPlugin(GrPluginCannotReprojectModel{ pluginName = "MODEL_CANNOT_REPROJECT_MASK", priority = 880 })
GrRenderPlugin.AddPlugin(GrPluginPrimitiveCannotReproject{ pluginName = "PRIMITIVES_CANNOT_REPROJECT_MASK", priority = 890 })
GrRenderPlugin.AddPlugin(GrPluginPostFilter{ pluginName = "POSTFILTER", priority = 900 })
GrRenderPlugin.AddPlugin(GrPluginColorCorrection{ pluginName = "COLOR_CORRECTION", priority = 600, parentPluginName = "POSTFILTER" })
GrRenderPlugin.AddPlugin(GrPluginDepthOfField{ pluginName = "DEPTH_OF_FIELD", priority = 200, parentPluginName = "POSTFILTER" })
GrRenderPlugin.AddPlugin(GrPlugin2DShrink{ pluginName = "DRAW2D_SHRINK", priority = 400, parentPluginName = "POSTFILTER" })
GrRenderPlugin.AddPlugin(GrPluginMotionBlur{ pluginName = "MOTION_BLUR", priority = 500, parentPluginName = "POSTFILTER" })
GrRenderPlugin.AddPlugin(GrPluginHairReconstructionModel{ pluginName = "HAIR_RECONSTRUCTION_MODEL", priority = 905 })
GrRenderPlugin.AddPlugin(GrPluginTonemap{ pluginName = "TONEMAP", priority = 910 })
GrRenderPlugin.AddPlugin(GrPluginAntiAliasing{ pluginName = "ANTI_ALIASING", priority = 930 })
GrRenderPlugin.AddPlugin(GrPluginOverlayModel{ pluginName = "OVERLAY_MODEL", priority = 940 })
GrRenderPlugin.AddPlugin(GrPluginPrimitiveUnfiltered{ pluginName = "PRIMITIVES_UNFILTERED", priority = 950, isEnableResolveRenderBuffer = false })
GrRenderPlugin.AddPlugin(GrPlugin2D{ pluginName = "DRAW2D", priority = 1573 })
GrRenderPlugin.AddPlugin(GrPlugin2DFrontmost{ pluginName = "DRAW2D_FRONTMOST", priority = 10000 })

-- ===================== TABLAS DE CALIDAD (solo claves que el exe lee) =====================
if GrGraphicsSettingManager then
GrGraphicsSettingManager.SetPluginSettingSelection(
	{
		allSettings = {
			{
				settingName = "PluginShadow",
				settingTable = {
					{ name = "Low", SpotLightLightShadowResolution=512, PointLightLightShadowResolution=1024, EnableCascadeShadowBlend=0.0, CascadeShadowRangeScale=1.0, DirectionalLightShadowResolution=1024 },
					{ name = "Medium", SpotLightLightShadowResolution=1024, PointLightLightShadowResolution=2048, EnableCascadeShadowBlend=0.0, CascadeShadowRangeScale=1.0, DirectionalLightShadowResolution=2048 },
					{ name = "Default", SpotLightLightShadowResolution=2048, PointLightLightShadowResolution=2048, EnableCascadeShadowBlend=0.0, CascadeShadowRangeScale=1.0, DirectionalLightShadowResolution=4096 },
					{ name = "High", SpotLightLightShadowResolution=SOMBRA_FOCOS, PointLightLightShadowResolution=SOMBRA_PUNTUAL, EnableCascadeShadowBlend=CASCADA_MEZCLA, CascadeShadowRangeScale=CASCADA_ALCANCE, DirectionalLightShadowResolution=SOMBRA_SOL },
				},
			},
			{
				settingName = "PluginPlanarShadow",
				settingTable = {
					{ name = "Low", ShadowQuality=0 },
					{ name = "Medium", ShadowQuality=0 },
					{ name = "Default", ShadowQuality=0 },
					{ name = "High", ShadowQuality=2 },
				},
			},
			{
				settingName = "PluginPesShadow",
				settingTable = {
					{ name = "Low", ShadowResolution=1024 },
					{ name = "Medium", ShadowResolution=2048 },
					{ name = "Default", ShadowResolution=4096 },
					{ name = "High", ShadowResolution=SOMBRA_JUGADORES },
				},
			},
			{
				settingName = "PluginSphericalHarmonics",
				settingTable = {
					{ name = "Low", DrawSphericalHarmonicsMaxCount=127, RejectionLengthBias=0.0 },
					{ name = "Medium", DrawSphericalHarmonicsMaxCount=127, RejectionLengthBias=0.0 },
					{ name = "Default", DrawSphericalHarmonicsMaxCount=127, RejectionLengthBias=0.0 },
					{ name = "High", DrawSphericalHarmonicsMaxCount=255, RejectionLengthBias=100.0 },
				},
			},
			{
				settingName = "PluginLocalLight",
				settingTable = {
					{ name = "Low", LocalLightLodMinLevel=0, RejectionLengthBias=0.0, LocalLightLodLevelBias=0.0, ShadowCastingDynamicLocalLightMaxCount=2, DrawLocalLightMaxCount=140, ShadowCastingStaticLocalLightMaxCount=2 },
					{ name = "Medium", LocalLightLodMinLevel=0, RejectionLengthBias=0.0, LocalLightLodLevelBias=0.0, ShadowCastingDynamicLocalLightMaxCount=2, DrawLocalLightMaxCount=524, ShadowCastingStaticLocalLightMaxCount=4 },
					{ name = "Default", LocalLightLodMinLevel=0, RejectionLengthBias=0.0, LocalLightLodLevelBias=0.0, ShadowCastingDynamicLocalLightMaxCount=4, DrawLocalLightMaxCount=524, ShadowCastingStaticLocalLightMaxCount=8 },
					{ name = "High", LocalLightLodMinLevel=0, RejectionLengthBias=200.0, LocalLightLodLevelBias=-2.0, ShadowCastingDynamicLocalLightMaxCount=LUCES_SOMBRA_MOVILES, DrawLocalLightMaxCount=LUCES_MAX, ShadowCastingStaticLocalLightMaxCount=LUCES_SOMBRA_FIJAS },
				},
			},
			{
				settingName = "PluginModel",
				settingTable = {
					{ name = "Low", RejectionLengthBias=0.0 },
					{ name = "Medium", RejectionLengthBias=32.0 },
					{ name = "Default", RejectionLengthBias=64.0 },
					{ name = "High", RejectionLengthBias=DIST_MODELOS },
				},
			},
			{
				settingName = "PluginClone",
				settingTable = {
					{ name = "Low", RejectionLengthBias=0.0 },
					{ name = "Medium", RejectionLengthBias=64.0 },
					{ name = "Default", RejectionLengthBias=64.0 },
					{ name = "High", RejectionLengthBias=DIST_MODELOS },
				},
			},
			{
				settingName = "PluginDof",
				settingTable = {
					{ name = "Off", EnableFilter=0, QualityType=0 },
					{ name = "Low", EnableFilter=1, QualityType=0 },
					{ name = "Medium", EnableFilter=1, QualityType=0 },
					{ name = "Default", EnableFilter=1, QualityType=2 },
					{ name = "High", EnableFilter=1, QualityType=DOF_CALIDAD },
				},
			},
			{
				settingName = "PluginFxaa",
				settingTable = {
					{ name = "Off", EnableFilter=0 },
					{ name = "Low", EnableFilter=1 },
					{ name = "Medium", EnableFilter=1 },
					{ name = "Default", EnableFilter=1 },
					{ name = "High", EnableFilter=1 },
				},
			},
			{
				settingName = "PluginSsao",
				settingTable = {
					{ name = "Off", EnableFilter=0 },
					{ name = "Low", EnableFilter=1 },
					{ name = "Medium", EnableFilter=1 },
					{ name = "Default", EnableFilter=1 },
					{ name = "High", EnableFilter=1 },
				},
			},
			{
				settingName = "PluginSao",
				settingTable = {
					{ name = "Off", EnableFilter=0, QualityType=0 },
					{ name = "Low", EnableFilter=0, QualityType=0 },
					{ name = "Medium", EnableFilter=1, QualityType=1 },
					{ name = "Default", EnableFilter=1, QualityType=1 },
					{ name = "High", EnableFilter=1, QualityType=SAO_CALIDAD },
				},
			},
			{
				settingName = "PluginMotionBlur",
				settingTable = {
					{ name = "Off", EnableFilter=0, BaseAmount=0 },
					{ name = "Low", EnableFilter=0, BaseAmount=0 },
					{ name = "Medium", EnableFilter=1, BaseAmount=0 },
					{ name = "Default", EnableFilter=1, BaseAmount=0.01 },
					{ name = "High", EnableFilter=1, BaseAmount=BLUR_BASE },
				},
			},
			{
				settingName = "MotionBlurAmount",
				settingTable = {
					{ name = "Off", BlurAmount=0 },
					{ name = "Small", BlurAmount=0.25 },
					{ name = "Medium", BlurAmount=0.5 },
					{ name = "Large", BlurAmount=1.0 },
					{ name = "Default", BlurAmount=1.0 },
				},
			},
			{
				settingName = "PluginToneMap",
				settingTable = {
					{ name = "Off", EnableBloom=0 },
					{ name = "Low", EnableBloom=0 },
					{ name = "Medium", EnableBloom=1 },
					{ name = "Default", EnableBloom=1 },
					{ name = "High", EnableBloom=1 },
				},
			},
			{
				settingName = "PluginSubsurfaceScatter",
				settingTable = {
					{ name = "Off", EnableFilter=0, FadeOffset=0 },
					{ name = "Low", EnableFilter=0, FadeOffset=0 },
					{ name = "Medium", EnableFilter=0, FadeOffset=0 },
					{ name = "Default", EnableFilter=SSS_MOTOR, FadeOffset=1 },
					{ name = "High", EnableFilter=SSS_MOTOR, FadeOffset=SSS_FADE },
				},
			},
			{
				settingName = "PluginCrowd",
				settingTable = {
					{ name = "Low", QualityType=0 },
					{ name = "Medium", QualityType=1 },
					{ name = "Default", QualityType=1 },
					{ name = "High", QualityType=PUBLICO_CALIDAD },
				},
			},
		}
	}
)
end
