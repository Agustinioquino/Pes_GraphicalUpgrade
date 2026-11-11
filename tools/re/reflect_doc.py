#!/usr/bin/env python3
"""reflect_doc.py - genera reflexion.md a partir de reflexion.tsv (salida de reflect.py).

Uso: python3 reflect_doc.py [reflexion.tsv] [reflexion.md]
Clasifica cada clase en una categoria y da una frase por propiedad: glosario exacto por nombre
(GLOS / GLOS_CLASE) y, si no hay, una frase armada con las palabras del nombre (marcada "(por nombre)").
"""
import sys, os, csv, re
from collections import defaultdict, OrderedDict

TSV = sys.argv[1] if len(sys.argv) > 1 else "/home/claude/re/out/reflexion.tsv"
MD = sys.argv[2] if len(sys.argv) > 2 else TSV.replace(".tsv", ".md")

CATS = OrderedDict([
    ("luz", "Luz (luces puntuales/direccionales/spot, sondas, Enlighten, reflejos)"),
    ("cielo_clima", "Cielo, atmosfera, hora del dia, niebla y lluvia"),
    ("sombras", "Sombras"),
    ("postprocesado", "Postprocesado (exposicion, tonemap, bloom, color, AO)"),
    ("camara", "Camara"),
    ("cesped_estadio", "Cesped, estadio, publico, red de arco"),
    ("mallas_modelos", "Mallas, modelos, formas, oclusores, transformaciones"),
    ("huesos_animacion", "Huesos y animacion"),
    ("fisica_ropa", "Fisica y ropa"),
    ("texturas_materiales", "Texturas y materiales"),
    ("gameplay", "Gameplay (demos fijas, situaciones, trampas, rutas)"),
    ("sistema", "Sistema (nucleo Fox: datos, bloques, contenedores, diferencias)"),
    ("otro", "Otro"),
])

CLASS_CAT = {
    # luz
    "PointLight": "luz", "PointLightBody": "luz", "DirectionalLight": "luz", "DirectionalLightBody": "luz",
    "SpotLight": "luz", "SpotLightBody": "luz", "LightProbe": "luz", "LightProbeBody": "luz", "LightArray": "luz",
    "GlobalLightArray": "luz", "GlobalLightArrayBody": "luz", "LightOccludeCube": "luz", "LightOccludeCubeBody": "luz",
    "ReflectionProbe": "luz", "ReflectionProbeSet": "luz", "GrReflectionSetting": "luz",
    "GrSphericalHarmonicsSettings": "luz", "EnlightenSystemContainer": "luz", "EnlightenModel": "luz",
    "EnlightenModelBody": "luz", "EnlightenProbeSet": "luz", "EnvmapLight": "luz", "TppLightProbe": "luz",
    "TppLightProbeBody": "luz", "TppLightProbeArray": "luz", "TppLightProbeSHCoefficients": "luz",
    "GrViewportLightingParameters": "luz", "EnlightenStadiumModel": "luz",
    # cielo / clima
    "AtmosphereSimulation": "cielo_clima", "AtmosphereSimulationBody": "cielo_clima", "TppAtmosphere": "cielo_clima",
    "TppAtmosphereBody": "cielo_clima", "TimeOfDaySettings": "cielo_clima", "TimeOfDay": "cielo_clima",
    "TimeSlotSettings": "cielo_clima", "TimeSlotGroup": "cielo_clima", "TimeSpan": "cielo_clima",
    "TimeOfDayObjectProxy": "cielo_clima", "TimeController": "cielo_clima", "TppSky": "cielo_clima",
    "GrSkyParameter": "cielo_clima", "Fog": "cielo_clima", "PesGlobalFog": "cielo_clima",
    "AtmosphericFogParams": "cielo_clima", "TppGlobalVolumetricFogParam": "cielo_clima",
    "TppGlobalVolumetricFog": "cielo_clima", "TppAreaVolumetricFogParam": "cielo_clima",
    "TppAreaVolumetricFog": "cielo_clima", "PesRainFilter": "cielo_clima", "Horizon": "cielo_clima",
    # sombras
    "GrShadowSettings": "sombras", "GrPrecomputeShadowSettings": "sombras", "GrPrecomputeShadowArea": "sombras",
    "ModelShadow": "sombras",
    # post
    "PesExposureSettings": "postprocesado", "TppTonemap": "postprocesado", "GrPluginSettings": "postprocesado",
    "PesPluginSettings": "postprocesado", "ColorCorrectionData": "postprocesado", "ColorGrading": "postprocesado",
    "AmbientOcclusionSettings": "postprocesado", "GrxLineSSAOParameters": "postprocesado",
    "GrxAreaSSAOParameters": "postprocesado", "GrViewportRenderingParameters": "postprocesado",
    # camara
    "GlobalCameraSettings": "camara", "SpecificCameraParameter": "camara", "FixDemoCameraData": "camara",
    # cesped / estadio
    "PitchMaterialParameter": "cesped_estadio", "PitchParameter": "cesped_estadio", "StadiumModel": "cesped_estadio",
    "StadiumModelBody": "cesped_estadio", "StadiumAnime": "cesped_estadio", "StadiumModelArray": "cesped_estadio",
    "StadiumTexture": "cesped_estadio", "AudienceModel": "cesped_estadio", "AudienceAreaData": "cesped_estadio",
    "BillModel": "cesped_estadio", "BillAnimeData": "cesped_estadio", "StandsObjectModel": "cesped_estadio",
    "MobCharacterModel": "cesped_estadio", "GoalnetData": "cesped_estadio", "GoalnetDataBody": "cesped_estadio",
    "LimitedRotatableObjectLinks": "cesped_estadio", "BallData": "cesped_estadio",
    # mallas
    "Occluder": "mallas_modelos", "OccluderEx": "mallas_modelos", "OccluderArray": "mallas_modelos",
    "CharacterAsset": "mallas_modelos", "Transform": "mallas_modelos", "TransformRT": "mallas_modelos",
    "Rotation": "mallas_modelos", "ShearTransform": "mallas_modelos", "PivotTransform": "mallas_modelos",
    "TransformEntity": "mallas_modelos", "TransformRTEntity": "mallas_modelos",
    "ShearTransformEntity": "mallas_modelos", "PivotTransformEntity": "mallas_modelos",
    "BoundingBox": "mallas_modelos", "Sphere": "mallas_modelos", "Capsule": "mallas_modelos",
    "OrientedBoundingBox": "mallas_modelos", "SphereShape": "mallas_modelos", "SphereShapeBody": "mallas_modelos",
    "BoxShape": "mallas_modelos", "BoxShapeBody": "mallas_modelos", "CapsuleShape": "mallas_modelos",
    "CapsuleShapeBody": "mallas_modelos", "GeoShape": "mallas_modelos",
    # anim
    "AnimParam": "huesos_animacion", "AnimParamCollector": "huesos_animacion", "PoseOperatorParam": "huesos_animacion",
    "PoseOperatorMirrorParam": "huesos_animacion", "PoseOperatorRootRotParam": "huesos_animacion",
    "TimeSection": "huesos_animacion", "EventDataUnit": "huesos_animacion", "ParameterMaskElement": "huesos_animacion",
    "FacialMaskElement": "huesos_animacion", "FacialSettingData": "huesos_animacion",
    "FixDemoHumanControlData": "huesos_animacion",
    # materiales
    "Material": "texturas_materiales", "PhMaterialInfo": "fisica_ropa",
    # gameplay
    "FixDemoCutData": "gameplay", "FixDemoEnvData": "gameplay", "FixDemoTimingData": "gameplay",
    "FixDemoEffectData": "gameplay", "FixDemoFunctionData": "gameplay", "FixDemoHumanData": "gameplay",
    "FixDemoObjectData": "gameplay", "SituationDataSetting": "gameplay", "SituationRouteEdgeEvent": "gameplay",
    "RouteEvent": "gameplay", "SituationRouteNode": "gameplay", "SituationBallData": "gameplay",
    "SituationObjectData": "gameplay", "SituationPlayerData": "gameplay", "FxLocatorGroup": "otro",
    "FxPresetData": "otro", "FxVariationConverterDataElement": "otro", "FxLocatorData": "otro",
}
RULES = [(r"^Ph", "fisica_ropa"), (r"Cloth", "fisica_ropa"), (r"^Geo", "gameplay"), (r"^Graphx", "gameplay"),
         (r"^Collision", "fisica_ropa"), (r"PropertyDifference$|Entity$|^Data|^Bucket|^Block|^Passive|^Stage|^Script|"
                                          r"^Message|^Actor|^Game$|^Project$|^Group$|^File$|^Locator$|"
                                          r"^Random|^FoxFlag|^EntityDifference|^DataSet|^TransformData|^FoxData|"
                                          r"^CallbackProcedure", "sistema"),
         (r"^Fx", "otro"), (r"^Gr", "postprocesado")]


def cat_of(c):
    if c in CLASS_CAT:
        return CLASS_CAT[c]
    for rx, k in RULES:
        if re.search(rx, c):
            return k
    return "otro"


CLASS_DESC = {
    "TimeOfDaySettings": "Ajustes por franja horaria del partido (cobra::rd): pisa sol, exposicion, tonemap ACES, niebla, SAO e intensidades de pantallas/carteles cuando el flag override* correspondiente esta en 1.",
    "TppAtmosphere": "Atmosfera de Fox (tpp::ef): dispersion Rayleigh/Mie, posicion geografica y fecha (calculo del sol/luna), sombras de la luz direccional y luz del cielo.",
    "AtmosphereSimulation": "Version grx de la simulacion de atmosfera (cielo fisico + luz direccional + niebla), mismos conceptos que TppAtmosphere.",
    "GrShadowSettings": "Ajustes de filtrado de sombras (opacidad, contraste, desenfoque, bias de profundidad, muestras temporales).",
    "GrPrecomputeShadowSettings": "Igual que GrShadowSettings para la sombra precalculada (estatica).",
    "PesExposureSettings": "Exposicion automatica por grupo (adaptacion de ojo): compensacion, limites, valor clave.",
    "TppTonemap": "Tonemap simple de Tpp: umbral y rango.",
    "TimeSlotSettings": "Horas del dia (hora/minuto/segundo y modo de luz solar) para cada tramo del partido: previa, 1.er tiempo, 2.o tiempo, alargue, penales y final.",
    "PitchMaterialParameter": "Parametros del shader del cesped: emisivo, AO por altura, repeticion UV, planitud de normales, marchitado (withers) y SRM.",
    "PitchParameter": "Ocho parametros genericos del cesped (pitchParam1..8) leidos por el render del campo (sentido exacto sin confirmar).",
    "GrPluginSettings": "Ajustes de los plugins de render de Fox (motion blur, bloom, exposicion, tonemap, decals, reflejos locales).",
    "PesPluginSettings": "Variante PES de GrPluginSettings (bloom con extraccion de brillo y peso).",
    "DirectionalLight": "Luz direccional (sol) colocada en un .fox2, con sus cascadas de sombra.",
    "SpotLight": "Luz foco (reflectores del estadio, etc.).",
    "PointLight": "Luz puntual.",
    "ModelShadow": "Sombra de modelo simple (proyectada) de PES, con atenuacion por altura y desenfoque.",
}

# glosario exacto (nombre de propiedad -> frase). GLOS_CLASE tiene prioridad (clase.prop).
GLOS = {
    "enable": "Activa o desactiva el elemento.", "enabled": "Activa o desactiva el elemento.",
    "name": "Nombre de la entidad.", "flags": "Campo de bits de opciones (los Bool virtuales leen/escriben bits de aca).",
    "localFlags": "Bits de opciones locales (los Bool virtuales de la clase viven aca).",
    "priority": "Prioridad frente a otras instancias del mismo tipo (gana la mayor).",
    "color": "Color (RGBA float).", "temperature": "Temperatura de color de la luz en kelvin.",
    "colorDeflection": "Desvio del color respecto del blanco de la temperatura (tinte).",
    "lux": "Iluminancia de la luz en lux.", "lumen": "Flujo luminoso de la luz en lumen.",
    "lightSize": "Tamano fisico de la fuente (ablanda la sombra/especular).",
    "castShadow": "La luz proyecta sombra.", "isBounced": "La luz participa en el rebote de GI (Enlighten).",
    "showObject": "Muestra el gizmo en el editor.", "showRange": "Muestra el rango en el editor.",
    "direction": "Direccion (vector) u orientacion.", "reachPoint": "Punto hasta donde llega la luz (define el eje).",
    "outerRange": "Radio exterior: la luz llega a 0 aca.", "innerRange": "Radio interior de intensidad plena.",
    "dimmer": "Multiplicador de intensidad (atenuador).", "shadowBias": "Sesgo de profundidad de la sombra (evita acne).",
    "viewBias": "Sesgo de la sombra en espacio de vista.", "powerScale": "Escala de potencia de la luz.",
    "LodFarSize": "Tamano en pantalla bajo el cual la luz se apaga (LOD lejano).",
    "LodNearSize": "Tamano en pantalla del LOD cercano.", "LodShadowDrawRate": "Proporcion de LOD a la que deja de dibujar sombra.",
    "lodRadiusLevel": "Nivel de LOD por radio.", "lodFadeType": "Tipo de fundido al cambiar de LOD.",
    "hasSpecular": "La luz aporta especular.", "lightArea": "Area/volumen al que pertenece la luz (enlace).",
    "lookAtPoint": "Entidad hacia la que apunta el foco.", "umbraAngle": "Angulo del cono de intensidad plena.",
    "penumbraAngle": "Angulo del cono de penumbra.", "attenuationExponent": "Exponente de caida de intensidad.",
    "shadowUmbraAngle": "Angulo de umbra usado para la sombra.", "shadowPenumbraAngle": "Angulo de penumbra usado para la sombra.",
    "shadowAttenuationExponent": "Exponente de caida usado para la sombra.", "useAutoDimmer": "Atenuacion automatica.",
    "isDebugLightVolumeBound": "Dibuja el volumen de la luz (depuracion).", "isDebugLightVolumeBounding": "Dibuja el volumen de la luz (depuracion).",
    "shadowRange": "Distancia cubierta por las cascadas de sombra de la luz direccional (m).",
    "shadowRangeExtra": "Distancia extra de la ultima cascada.", "hiResShadowRange": "Distancia de la cascada de alta resolucion.",
    "shadowProjectionRange": "Profundidad de proyeccion del mapa de sombras.", "shadowFadeRange": "Tramo final donde la sombra se desvanece.",
    "selfShadowBias": "Sesgo de autosombra.", "isCascadeBlend": "Mezcla entre cascadas para ocultar el corte.",
    "shadowMaskSpecular": "Cuanto oscurece la sombra al especular.", "shadowOffsetStartAngle": "Angulo del sol donde empieza el desplazamiento de la sombra.",
    "shadowOffsetEndAngle": "Angulo del sol donde termina el desplazamiento de la sombra.",
    "enableDistanceFade": "Apaga la luz con la distancia.", "diatanceFade_StartDistance": "Distancia donde empieza el fundido (el typo esta en el exe).",
    "distanceFade_FadeoutRange": "Largo del fundido por distancia.",
    "rayleighHeightScale": "Altura de escala de la capa Rayleigh (aire).", "rayleighScatteringCoefficient": "Coeficiente de dispersion Rayleigh RGB (color azul del cielo).",
    "rayleighScatteringCoefficientOfCloudySky": "Coeficiente Rayleigh con cielo nublado.", "rayleighHeightScaleOfCloudySky": "Altura de escala Rayleigh con cielo nublado.",
    "mieHeightScale": "Altura de escala de la capa Mie (aerosoles).", "mieAbsorptionRate": "Absorcion de Mie.",
    "mieAnisotropy": "Anisotropia de Mie (halo alrededor del sol, g de Henyey-Greenstein).",
    "mieAnisotropyOfCloudySky": "Anisotropia de Mie con cielo nublado.", "mieScatteringCoefficient": "Coeficiente de dispersion Mie RGB (bruma).",
    "mieScatteringCoefficientOfCloudySky": "Coeficiente Mie con cielo nublado.", "mieHeightScaleOfCloudySky": "Altura de escala Mie nublado.",
    "mieAbsorptionRateOfCloudySky": "Absorcion de Mie nublado.", "multiScatteringOrder": "Ordenes de dispersion multiple en la tabla del cielo.",
    "groundColor": "Albedo del suelo para la luz del cielo.", "moonColor": "Color de la luna.",
    "sunColorExtinctionOfClearSky": "Color/extincion del sol con cielo despejado.", "sunColorOfCloudySky": "Color del sol con cielo nublado.",
    "northAngle": "Angulo del norte respecto del eje Z del mundo (orienta el sol).",
    "latitude": "Latitud geografica (grados) usada para la posicion del sol/luna.",
    "longitude": "Longitud geografica (grados) usada para la posicion del sol/luna.",
    "altitude": "Altitud del observador.", "gmtTimeDifference": "Huso horario (horas respecto de GMT).",
    "year": "Ano de la fecha simulada.", "month": "Mes de la fecha simulada.", "day": "Dia de la fecha simulada.",
    "hour": "Hora simulada.", "minute": "Minuto simulado.", "second": "Segundo simulado.", "timeSpeed": "Velocidad del reloj simulado.",
    "influenceOfFog": "Influencia de la atmosfera en la niebla.", "sunLux": "Iluminancia del sol en lux.",
    "moonLux": "Iluminancia de la luna en lux.", "starLight": "Luz de las estrellas.",
    "skyLightSunScale": "Escala de la luz del cielo aportada por el sol.", "skyColorSunScale": "Escala del color del cielo por el sol.",
    "daySkyAmbientScale": "Escala del ambiente del cielo de dia.", "dirLightSunLimitAngle": "Angulo minimo del sol sobre el horizonte para la luz direccional.",
    "dirLightMoonLimitAngle": "Angulo minimo de la luna para la luz direccional.", "dirLightAttenuStart": "Altura angular donde empieza a atenuarse la direccional.",
    "dirLightAttenuEnd": "Altura angular donde termina la atenuacion.", "dirLightFadeStart": "Inicio del fundido de la direccional.",
    "dirLightFadeLength": "Largo del fundido de la direccional.", "cloudiness": "Nubosidad 0..1 (mezcla cielo despejado/nublado).",
    "skyLightLuminanceScale": "Escala de luminancia de la luz del cielo.", "interpolateTimeInSecondOfDirLightSteppedMove": "Segundos de interpolacion de cada paso del sol.",
    "divisonNumOfDirLightSteppedMove": "Cantidad de pasos del movimiento escalonado del sol.", "numBands": "Bandas de armonicos esfericos (SH).",
    "coefficients": "Coeficientes SH (ambiente).", "atshFilePath": "Ruta de la tabla de dispersion precalculada (.atsh).",
    "pcspFilePath": "Ruta del archivo de cielo precalculado (.pcsp).", "atshFilePtr": "Archivo .atsh cargado.", "pcspFilePtr": "Archivo .pcsp cargado.",
    "capturePosition": "Entidad desde donde se captura el cielo.", "fixedLightDirSunRise": "Direccion fija del sol al amanecer.",
    "fixedLightDirSunSet": "Direccion fija del sol al atardecer.", "fixedLightDirMoonRise": "Direccion fija de la luna al salir.",
    "fixedLightDirMoonSet": "Direccion fija de la luna al ponerse.", "fixedRisingSunDir": "Direccion fija del sol subiendo.",
    "fixedFallingSunDir": "Direccion fija del sol bajando.", "fixedRisingMoonDir": "Direccion fija de la luna subiendo.",
    "fixedFallingMoonDir": "Direccion fija de la luna bajando.", "useBakedData": "Usa los datos de cielo horneados (.atsh/.pcsp).",
    "skyEnable": "Dibuja el cielo.", "sunLightEnable": "Activa la luz direccional del sol.", "dirLightFade": "Fundido de la direccional en el horizonte.",
    "disableSkyCapture": "No captura el cielo para reflejos.", "skyLightEnable": "Activa la luz del cielo (ambiente).",
    "usePrecomputedAmbient": "Usa el ambiente precalculado.", "fogEnable": "Activa la niebla.", "expandHorizontalLineColor": "Extiende el color del horizonte hacia abajo.",
    "isSteppedMoveOfDirectionalLight": "Mueve el sol a saltos (evita parpadeo de sombras).",
    "fogDensity": "Densidad de la niebla.", "fogAltitude": "Altura base de la niebla.", "fogHeightScale": "Caida de la niebla con la altura.",
    "presetMoon": "Usa la luna predefinida.", "newMoonLux": "Lux con luna nueva.", "fullMoonLux": "Lux con luna llena.",
    "moonTemperature": "Temperatura de color de la luna.", "moonColorDeflection": "Tinte de la luna.", "nightSkyScale": "Escala del cielo nocturno.",
    "disableStarLight": "Apaga la luz de estrellas.", "nightSkyLightScale": "Escala de la luz de cielo nocturna.", "skyLightScale": "Escala de la luz del cielo.",
    # sombras
    "method": "Metodo/algoritmo (enum).", "shadowOpacity": "Opacidad de la sombra (0 = invisible, 1 = negra).",
    "shadowContrast": "Contraste de la sombra.", "shadowBlurRadius": "Radio de desenfoque de la sombra (suavidad).",
    "maskedShadowOpacity": "Opacidad de la sombra en zonas enmascaradas.", "maskedShadowContrast": "Contraste en zonas enmascaradas.",
    "maskedShadowBlurRadius": "Desenfoque en zonas enmascaradas.", "depthBias": "Sesgo de profundidad al comparar el mapa de sombras (acne vs peter-panning).",
    "remappingThreshold": "Umbral de remapeo de la sombra.", "temporalSampleCount": "Muestras temporales del filtro de sombra (TAA de sombras).",
    # exposicion / post
    "exposureCompensation": "Compensacion de exposicion (EV).", "minExposure": "Exposicion minima de la adaptacion automatica.",
    "maxExposure": "Exposicion maxima de la adaptacion automatica.", "keyValue": "Valor clave (gris medio) del autoexposure.",
    "maxLuminanceValue": "Luminancia maxima considerada.", "initialExposureValue": "Exposicion inicial al cargar.", "groupId": "Grupo de exposicion al que aplica.",
    "threshold": "Umbral.", "range": "Rango.", "motionBlurConvolutionLevel": "Niveles de convolucion del motion blur.",
    "bloomWhiteOffset": "Desplazamiento de blanco del bloom.", "bloomColorCapacity": "Capacidad de color del bloom.",
    "bloomSize": "Tamano del bloom.", "bloomBrightnessExtraction": "Umbral de brillo que alimenta el bloom.", "bloomWeight": "Peso del bloom en la mezcla.",
    "tonemapSpeed": "Velocidad de adaptacion del tonemap/exposicion.", "minDecalArea": "Area minima para dibujar un decal.",
    "captureBounceCount": "Rebotes al capturar reflejos.", "isBloom": "Activa bloom.", "isDOFVisualizeFocus": "Visualiza el foco del DOF.",
    "isDepthOfField": "Activa profundidad de campo.", "isFixedShutterRatio": "Obturacion fija para el motion blur.",
    "isLightAdaptationFromLACC": "Adaptacion de luz desde LACC.", "isLocalReflections": "Activa reflejos locales (SSR).",
    "isMotionBlur": "Activa motion blur.", "isPatchVelocity": "Corrige velocidades (motion blur).", "isShowDecals": "Dibuja decals.",
    "isShrinkSHBuffer": "Reduce el buffer SH.", "isTonemap": "Activa el tonemap.",
    "colorScale": "Escala de color.", "startSlope": "Pendiente inicial de la curva.", "endSlope": "Pendiente final de la curva.",
    "textureLUT": "LUT de color (textura).", "showBaseLUT": "Muestra la LUT base.", "showFilterLUT": "Muestra la LUT de filtro.",
    "sdrGameGradingLut": "LUT de gradacion SDR para el juego.", "sdrDemoGradingLut": "LUT SDR para repeticiones/demos.",
    "hdrGameGradingLut": "LUT HDR para el juego.", "hdrDemoGradingLut": "LUT HDR para demos.",
    # camara
    "focalDistance": "Distancia de foco (DOF).", "focalLength": "Distancia focal (mm).", "aperture": "Apertura (f-stop) para el DOF.",
    "shutterSpeed": "Velocidad de obturacion (motion blur).", "isIgnoreDofCameraSetting": "Ignora el DOF de la camara.",
    "isIgnoreMotionBlurCameraSetting": "Ignora el motion blur de la camara.",
    # niebla
    "near": "Distancia cercana.", "far": "Distancia lejana.", "limitMin": "Limite minimo.", "limitMax": "Limite maximo.",
    "density": "Densidad.", "heightScale": "Caida con la altura.", "mode": "Modo (enum o entero).", "startDepth": "Profundidad donde empieza la niebla.",
    "rangeDepthFar": "Rango de profundidad lejano.", "rangeDepthNear": "Rango de profundidad cercano.", "rangeDepthNear2": "Segundo rango cercano.",
    "fogAmpFar": "Amplitud de niebla lejos.", "fogAmpNear": "Amplitud de niebla cerca.", "whiteness": "Blancura de la niebla.",
    "objFogRgbAmpFuncScale": "Escala de la funcion de amplitud RGB sobre objetos.", "objFogRgbAmp2FuncScale": "Idem segunda funcion.",
    "objFogHeightAmpMax": "Amplitud maxima por altura sobre objetos.", "objFogHeightAmpMaxHeight": "Altura de esa amplitud maxima.",
    "objFogRgbAmp": "Amplitud RGB de la niebla sobre objetos.", "objFogRgbAmp2": "Segunda amplitud RGB.",
    "falloff": "Caida.", "selfColor": "Color propio.", "selfLuminance": "Luminancia propia.", "mieScattering": "Dispersion Mie (color).",
    "rayleighScattering": "Dispersion Rayleigh (color).", "skyAlbedo": "Albedo del cielo.", "dirLightGain": "Ganancia de la luz direccional.",
    "skyLightGain": "Ganancia de la luz del cielo.", "lightningGain": "Ganancia de los relampagos.",
    # lluvia
    "albedoExtinctionRatio": "Cuanto oscurece la lluvia el albedo (superficie mojada).",
    "roughnessEffectiveThreshold": "Rugosidad desde la que actua el mojado.", "roughnessExtinctionCoefficient": "Cuanto baja la rugosidad al mojarse.",
    "startFadeInDistance": "Distancia donde empieza a aparecer.", "endFadeInDistance": "Distancia donde aparece completo.",
    "startFadeOutDistance": "Distancia donde empieza a desaparecer.", "endFadeOutDistance": "Distancia donde desaparece.",
    # cielo tpp
    "cloudCover": "Cobertura de nubes.", "diffusion": "Difusion de las nubes.", "inCloudScatterGain": "Ganancia de dispersion dentro de la nube.",
    "cloudWindInfluence": "Influencia del viento en las nubes.", "textureRepeats": "Repeticiones de la textura.", "model": "Modelo (archivo).",
    "cloudTexture": "Textura de nubes.",
    # modelos
    "modelFile": "Archivo de modelo (.fmdl) cargado.", "isIsolated": "Modelo aislado (sin instancias compartidas).", "isDynamic": "Modelo dinamico.",
    "lodNearSize": "Tamano en pantalla del LOD cercano.", "lodFarSize": "Tamano en pantalla del LOD lejano.", "lodPolygonSize": "Tamano de poligono para el LOD.",
    "drawRejectionLevel": "Nivel desde el que se deja de dibujar (calidad).", "drawMode": "Modo de dibujo.",
    "rejectFarRangeShadowCast": "No proyecta sombra a distancia.", "kind": "Tipo/categoria.", "demoGroup": "Grupo de demo al que pertenece.",
    "customBits": "Bits de uso libre del juego.", "guid": "GUID del modelo para Enlighten.",
    "parent": "Entidad padre en la jerarquia.", "transform": "Transformacion local.", "shearTransform": "Transformacion de cizalla.",
    "pivotTransform": "Transformacion de pivote.", "children": "Hijos en la jerarquia.", "inheritTransform": "Hereda la transformacion del padre.",
    "selection": "Seleccionado en el editor.", "visibility": "Visible.", "worldMatrix": "Matriz de mundo calculada.", "worldTransform": "Transformacion de mundo calculada.",
    "scale": "Escala.", "rotation": "Rotacion.", "translation": "Traslacion.", "quat": "Cuaternion de rotacion.", "rotQuat": "Rotacion en cuaternion.",
    "shear": "Cizalla.", "pivot": "Pivote.", "pivotTranslation": "Traslacion del pivote.", "min": "Minimo de la caja.", "max": "Maximo de la caja.",
    "center": "Centro.", "size": "Tamano.", "radius": "Radio.", "point0": "Primer extremo.", "point1": "Segundo extremo.",
    "intensity": "Intensidad.", "intensityClamp": "Tope de intensidad.", "texturePath": "Ruta de textura.",
    "value": "Valor contenido.", "dataSet": "DataSet al que pertenece.", "referencePath": "Ruta de referencia.", "owner": "Entidad duena.",
    "originalValues": "Valores originales (para deshacer la diferencia).", "values": "Valores.",
}
GLOS.update({
    "enlightenQuality": "Calidad de Enlighten (GI en tiempo real) para el modelo.", "enlightenModelType": "Rol del modelo en Enlighten: objetivo, detalle o cubierta.",
    "outputPixelSize": "Tamano del texel de lightmap de Enlighten.", "clusterSize": "Tamano de cluster de radiosidad.",
    "secondaryBounceScale": "Escala del rebote secundario de la GI.", "captureSource": "Fuente de la captura de la sonda (escena, ambiente de archivo o solo cubemap).",
    "cubemapFile": "Cubemap precalculado de la sonda.", "cubemapLuminanceScale": "Escala de luminancia del cubemap.",
    "luminanceScale": "Escala de luminancia de la sonda.", "debugMode": "Modo de depuracion (enum).",
    "minCaptureExposure": "Exposicion minima al capturar.", "exposure": "Exposicion.", "shapeType": "Forma del volumen (enum).",
    "captureResolution": "Resolucion del cubemap capturado.", "captureVolume": "Volumen de captura (enlace).",
    "enableParallaxCorrection": "Corrige la paralaje del reflejo con la caja de captura.", "relatedLights": "Luces asociadas a la sonda.",
    "shDatas": "Datos SH asociados.", "lightArrayFile": "Archivo de arreglo de luces precalculado.", "innerArea": "Area interior (enlace).",
    "occlusionRate": "Tasa de oclusion.", "voxelSize": "Tamano de voxel para ubicar sondas.", "enhancerEnabled": "Activa el realce de SH.",
    "enhancerRange": "Rango del realce de SH.", "enhancerAmount": "Cantidad de realce de SH.", "reflectionProbes": "Sondas de reflejo del conjunto.",
    "spotLights": "Luces foco del arreglo.", "skyColor": "Color del cielo.", "horizonColor": "Color del horizonte.", "colorPreset": "Preset de color (enum).",
    "timeAdvanceMode": "Como avanza la hora (nada, progresivo, tiempo real).", "startHour": "Hora de inicio del tramo.", "startMinute": "Minuto de inicio del tramo.",
    "settings": "Ajustes asociados (enlace).", "timeSpans": "Tramos horarios.", "albedo": "Albedo.", "luminance": "Luminancia.",
    "daySkyLightScale": "Escala de luz de cielo de dia.",
})

GLOS.update({
    "mass": "Masa del cuerpo rigido (kg).", "friction": "Coeficiente de friccion.", "restitution": "Coeficiente de rebote (0 = sin rebote).",
    "maxLinearVelocity": "Velocidad lineal maxima.", "maxAngularVelocity": "Velocidad angular maxima.",
    "linearVelocityDamp": "Amortiguacion de la velocidad lineal.", "angularVelocityDamp": "Amortiguacion de la velocidad angular.",
    "permittedDepth": "Penetracion permitida entre cuerpos antes de corregir.", "sleepLinearVelocityTh": "Umbral de velocidad lineal para dormir el cuerpo.",
    "sleepAngularVelocityTh": "Umbral de velocidad angular para dormir el cuerpo.", "sleepTimeTh": "Tiempo quieto antes de dormir el cuerpo.",
    "sleepEnable": "Permite dormir el cuerpo cuando queda quieto.", "collisionGroup": "Grupo de colision.", "collisionType": "Tipo de colision.",
    "collisionId": "Id de colision.", "centerOfMassOffset": "Desplazamiento del centro de masa.", "motionType": "Tipo de movimiento (estatico/dinamico/cinematico).",
    "material": "Material fisico (nombre).", "isNoGravity": "Ignora la gravedad.", "defaultPosition": "Posicion inicial.", "defaultRotation": "Rotacion inicial.",
    "axis": "Eje de la restriccion.", "limitHi": "Limite superior.", "limitLo": "Limite inferior.", "limitedFlag": "La restriccion tiene limites.",
    "isPoweredFlag": "La restriccion tiene motor.", "powerControlType": "Tipo de control del motor.", "controlType": "Tipo de control.",
    "targetTheta": "Angulo objetivo del motor.", "targetVelocity": "Velocidad objetivo del motor.", "torqueMax": "Torque maximo del motor.",
    "velocityMax": "Velocidad maxima.", "velocityRate": "Tasa de velocidad.", "param": "Parametros asociados (enlace).",
})

GLOS_CLASE = {
    "TimeOfDaySettings.overrideDateTime": "Si es 1, fuerza la hora dateTimeHour:dateTimeMinute en vez de la del partido.",
    "TimeOfDaySettings.dateTimeHour": "Hora forzada (0-23).", "TimeOfDaySettings.dateTimeMinute": "Minuto forzado.",
    "TimeOfDaySettings.overrideSunLightMode": "Si es 1, usa sunLightMode.",
    "TimeOfDaySettings.sunLightMode": "Modo de la luz solar: 0 normal, 1 solo precalculada, 2 apagada, 3 solo sombra en cascada.",
    "TimeOfDaySettings.overrideAtmosphere": "Si es 1, aplica sunLux/moonLux/skyLightSunScale/enlightenOutputScale.",
    "TimeOfDaySettings.sunLux": "Iluminancia del sol en lux para esta franja (pisa la de TppAtmosphere).",
    "TimeOfDaySettings.moonLux": "Iluminancia de la luna en lux.", "TimeOfDaySettings.skyLightSunScale": "Escala de la luz de cielo que aporta el sol.",
    "TimeOfDaySettings.enlightenOutputScale": "Multiplicador de la GI de Enlighten.",
    "TimeOfDaySettings.overrideGameExposure": "Si es 1, aplica la exposicion game* durante el juego.",
    "TimeOfDaySettings.overrideDemoExposure": "Si es 1, aplica la exposicion demo* en repeticiones/escenas.",
    "TimeOfDaySettings.overrideTonemap": "Si es 1, aplica la curva ACES y el balance de blancos.",
    "TimeOfDaySettings.acesFilmSlope": "Pendiente de la curva filmica ACES.", "TimeOfDaySettings.acesFilmToe": "Pie (sombras) de la curva ACES.",
    "TimeOfDaySettings.acesFilmShoulder": "Hombro (altas luces) de la curva ACES.", "TimeOfDaySettings.acesFilmBlackClip": "Recorte de negros ACES.",
    "TimeOfDaySettings.acesFilmWhiteClip": "Recorte de blancos ACES.", "TimeOfDaySettings.whiteTemperature": "Balance de blancos: temperatura.",
    "TimeOfDaySettings.whiteTint": "Balance de blancos: tinte.", "TimeOfDaySettings.overrideColorGrading": "Si es 1, usa colorGradingData.",
    "TimeOfDaySettings.colorGradingData": "Enlace a la entidad ColorGrading (LUTs) de la franja.",
    "TimeOfDaySettings.overrideGlobalFog": "Si es 1, usa globalFog*.", "TimeOfDaySettings.globalFogEnabled": "Activa la niebla global.",
    "TimeOfDaySettings.globalFogColor": "Color de la niebla global.", "TimeOfDaySettings.globalFogDensity": "Densidad de la niebla global.",
    "TimeOfDaySettings.globalFogHeightScale": "Caida con la altura de la niebla global.",
    "TimeOfDaySettings.overrideAtmosphericFog": "Si es 1, usa los parametros de niebla atmosferica que siguen.",
    "TimeOfDaySettings.overrideAmbientObscurance": "Si es 1, usa los parametros SAO.",
    "TimeOfDaySettings.saoRadius": "Radio del SAO (oclusion ambiental).", "TimeOfDaySettings.saoBias": "Sesgo del SAO.",
    "TimeOfDaySettings.saoIntensity": "Intensidad del SAO.", "TimeOfDaySettings.saoContrast": "Contraste del SAO.",
    "TimeOfDaySettings.saoBlurEnabled": "Desenfoque del SAO.", "TimeOfDaySettings.saoBlurSharpness": "Nitidez del desenfoque SAO.",
    "TimeOfDaySettings.overrideModelIntensity": "Si es 1, aplica las intensidades de pantallas y carteles.",
    "TimeOfDaySettings.modelIntensityTvBox": "Brillo de las pantallas de TV/marcador.", "TimeOfDaySettings.modelIntensityTvLine": "Brillo de las lineas de LED.",
    "TimeOfDaySettings.modelIntensityBillNormal": "Brillo de los carteles normales.", "TimeOfDaySettings.modelIntensityBillMovie": "Brillo de los carteles animados.",
    "TimeOfDaySettings.modelIntensityAd": "Brillo de la publicidad.", "TimeOfDaySettings.overrideAlphaModelReflections": "Si es 1, usa alphaModelReflections.",
    "TimeOfDaySettings.alphaModelReflections": "Enlace a los reflejos de modelos transparentes.",
    "TppTonemap.threshold": "Umbral de luminancia del tonemap.", "TppTonemap.range": "Rango de compresion del tonemap.",
    "TppTonemap.enable": "Activa el tonemap.",
    "PitchMaterialParameter.aoHightPower": "Potencia del AO por altura de la hoja (typo 'Hight' del exe).",
    "PitchMaterialParameter.aoMin": "AO minimo en la base de la hoja.", "PitchMaterialParameter.aoMax": "AO maximo en la punta.",
    "PitchMaterialParameter.aoBottomUp": "Altura de transicion del AO.", "PitchMaterialParameter.uRepeat": "Repeticion de la textura en U.",
    "PitchMaterialParameter.vRepeat": "Repeticion de la textura en V.", "PitchMaterialParameter.normalFlatness": "Aplana las normales del cesped (0 = relieve, 1 = plano).",
    "PitchMaterialParameter.repairColor": "Color de las zonas reparadas/gastadas.", "PitchMaterialParameter.cameraVectorBlend": "Mezcla de la normal hacia la camara (brillo rasante).",
    "PesExposureSettings.groupId": "Grupo de exposicion (juego, demo...).",
    "TimeSlotSettings.preview": "Tramo que se previsualiza en el editor.",
    "SpecificCameraParameter.cameraNames": "Camaras a las que aplica.", "SpecificCameraParameter.parameterName": "Nombre del parametro a pisar.",
    "SpecificCameraParameter.parameterValue": "Valor a aplicar.", "SpecificCameraParameter.restoreValue": "Valor a restaurar.",
}
TOK = {
    "shadow": "sombra", "light": "luz", "lights": "luces", "color": "color", "range": "rango", "scale": "escala", "intensity": "intensidad",
    "enable": "activar", "enabled": "activo", "is": "", "use": "usar", "file": "archivo", "path": "ruta", "ptr": "", "model": "modelo",
    "texture": "textura", "camera": "camara", "time": "tiempo", "start": "inicio", "end": "fin", "offset": "desplazamiento",
    "position": "posicion", "pos": "posicion", "rotation": "rotacion", "rot": "rotacion", "speed": "velocidad", "rate": "tasa",
    "min": "minimo", "max": "maximo", "size": "tamano", "radius": "radio", "type": "tipo", "mode": "modo", "count": "cantidad", "num": "cantidad",
    "name": "nombre", "names": "nombres", "id": "id", "index": "indice", "level": "nivel", "angle": "angulo", "distance": "distancia",
    "height": "altura", "width": "ancho", "length": "largo", "weight": "peso", "fade": "fundido", "blend": "mezcla", "bias": "sesgo",
    "target": "objetivo", "data": "datos", "param": "parametro", "params": "parametros", "parameter": "parametro", "list": "lista",
    "flag": "bandera", "flags": "banderas", "mass": "masa", "friction": "friccion", "restitution": "rebote", "damping": "amortiguacion",
    "linear": "lineal", "angular": "angular", "velocity": "velocidad", "gravity": "gravedad", "limit": "limite", "spring": "resorte",
    "constraint": "restriccion", "bone": "hueso", "bones": "huesos", "human": "humano", "player": "jugador", "ball": "pelota",
    "effect": "efecto", "demo": "demo", "cut": "corte", "frame": "cuadro", "event": "evento", "route": "ruta", "node": "nodo", "edge": "arista",
    "trap": "trampa", "condition": "condicion", "script": "script", "block": "bloque", "group": "grupo", "lod": "LOD", "near": "cercano",
    "far": "lejano", "draw": "dibujo", "density": "densidad", "fog": "niebla", "sky": "cielo", "sun": "sol", "moon": "luna", "probe": "sonda",
    "cube": "cubo", "map": "mapa", "lut": "LUT", "exposure": "exposicion", "bloom": "bloom", "dof": "DOF", "focus": "foco",
    "audience": "publico", "stand": "tribuna", "stands": "tribunas", "seat": "asiento", "bill": "cartel", "anime": "animacion",
    "anim": "animacion", "motion": "movimiento", "mask": "mascara", "direction": "direccion", "dir": "direccion", "vector": "vector",
    "point": "punto", "points": "puntos", "box": "caja", "sphere": "esfera", "capsule": "capsula", "shape": "forma", "mesh": "malla",
    "material": "material", "diffuse": "difuso", "normal": "normal", "specular": "especular", "roughness": "rugosidad",
    "emissive": "emisivo", "alpha": "alfa", "env": "entorno", "object": "objeto", "objects": "objetos", "locator": "localizador",
    "locators": "localizadores", "timing": "tiempo", "delay": "retraso", "duration": "duracion", "loop": "bucle", "random": "aleatorio",
    "seed": "semilla", "team": "equipo", "home": "local", "away": "visitante", "score": "goles", "member": "miembro", "entry": "entrada",
    "debug": "depuracion", "show": "mostrar", "visible": "visible", "hide": "ocultar", "value": "valor", "values": "valores",
}

TOK.update({
    "inner": "interior", "outer": "exterior", "ref": "referencia", "stadium": "estadio", "large": "grande", "small": "chico",
    "in": "en", "bytes": "bytes", "no": "sin", "ik": "IK", "left": "izquierda", "right": "derecha", "high": "alto", "hi": "alto",
    "low": "bajo", "lo": "bajo", "medium": "medio", "array": "arreglo", "back": "atras", "front": "frente", "set": "conjunto",
    "sets": "conjuntos", "main": "principal", "blur": "desenfoque", "positive": "positivo", "negative": "negativo",
    "limited": "limitado", "body": "cuerpo", "simp": "simplificacion", "default": "por defecto", "memo": "memo",
    "center": "centro", "check": "chequeo", "base": "base", "skl": "esqueleto", "skel": "esqueleto", "gskl": "esqueleto global",
    "cluster": "cluster", "kind": "clase", "zoom": "zoom", "zooming": "zoom", "opacity": "opacidad", "on": "encendido",
    "off": "apagado", "exec": "ejecutar", "collision": "colision", "sleep": "reposo", "tags": "etiquetas", "area": "area",
    "half": "tiempo (mitad)", "disp": "mostrar", "editable": "editable", "ratio": "proporcion", "radiosity": "radiosidad",
    "budget": "presupuesto", "parent": "padre", "axis": "eje", "torque": "torque", "th": "umbral", "models": "modelos",
    "handle": "handle", "output": "salida", "pixel": "pixel", "initial": "inicial", "string": "texto", "function": "funcion",
    "func": "funcion", "corner": "corner", "mover": "movil", "sh": "SH", "depth": "profundidad", "related": "relacionado",
    "enlighten": "Enlighten", "deviation": "desvio", "expansion": "expansion", "clip": "recorte", "fullshot": "plano entero",
    "cameraman": "camarografo", "others": "otros", "and": "y", "message": "mensaje", "reflection": "reflejo",
    "attenuation": "atenuacion", "capture": "captura", "powered": "motorizado", "vec": "vector", "damp": "amortiguacion",
    "endurance": "resistencia", "directory": "directorio", "margin": "margen", "loading": "carga", "scene": "escena",
    "groups": "grupos", "files": "archivos", "vertices": "vertices", "verts": "vertices", "reference": "referencia",
    "samples": "muestras", "per": "por", "property": "propiedad", "select": "elegir", "by": "por", "of": "de",
    "change": "cambio", "changing": "cambio", "changed": "cambiado", "roll": "giro (roll)", "audi": "publico",
    "resolution": "resolucion", "supporter": "hincha", "callback": "callback", "elements": "elementos", "add": "agregar",
    "enhancer": "realce", "falloff": "caida", "occlusion": "oclusion", "luminance": "luminancia", "power": "potencia",
    "actor": "actor", "actors": "actores", "ssao": "SSAO", "parameters": "parametros", "skeleton": "esqueleto",
    "tex": "textura", "paths": "rutas", "prerequisite": "prerrequisito", "prerequisites": "prerrequisitos",
    "memory": "memoria", "collector": "colector", "locked": "bloqueado", "bucket": "bucket", "buckets": "buckets",
    "links": "enlaces", "quality": "calidad", "general": "general", "factor": "factor", "significant": "significativa",
    "spacing": "separacion", "entity": "entidad", "differences": "diferencias", "difference": "diferencia",
    "shader": "shader", "top": "arriba", "correct": "corregir", "correction": "correccion", "for": "para", "lens": "lente",
    "moving": "en movimiento", "rear": "trasero", "play": "jugada", "flip": "espejar", "life": "vida", "shoot": "remate",
    "move": "mover", "ground": "suelo", "crane": "grua", "parts": "partes", "reset": "reiniciar", "once": "una vez",
    "action": "accion", "instance": "instancia", "user": "usuario", "source": "fuente", "preset": "preset",
    "category": "categoria", "priority": "prioridad", "done": "hecho", "package": "paquete", "volume": "volumen",
    "contrast": "contraste", "cubemap": "cubemap", "degree": "grado", "side": "lado", "flexibility": "flexibilidad",
    "constant": "constante", "custom": "personalizado", "stop": "parar", "twist": "torsion", "contraction": "contraccion",
    "control": "control", "theta": "theta", "world": "mundo", "indices": "indices", "permitted": "permitido",
    "pre": "pre", "to": "a", "restart": "reinicio", "anm": "animacion", "render": "render", "rig": "rig", "stage": "escenario",
    "settings": "ajustes", "transform": "transformacion", "quat": "cuaternion", "attachment": "anclaje", "line": "linea",
    "screen": "pantalla", "rgb": "RGB", "movie": "video", "order": "orden", "owner": "dueno", "attribute": "atributo",
    "identifier": "identificador", "voxel": "voxel", "merging": "fusion", "threshold": "umbral", "secondary": "secundario",
    "bounce": "rebote", "sections": "secciones", "int": "entero", "float": "float", "aspect": "aspecto", "mouth": "boca",
    "lip": "labio", "root": "raiz", "original": "original", "eq": "igual", "return": "retorno", "distortion": "distorsion",
    "auto": "automatico", "headup": "plano cabeza", "bustup": "plano busto", "waistup": "plano cintura",
    "kneeup": "plano rodilla", "field": "campo", "pattern": "patron", "interval": "intervalo", "number": "numero",
    "cheer": "aliento", "coach": "DT", "bench": "banco", "entrance": "salida a la cancha", "grass": "cesped", "halo": "halo",
    "star": "estrella", "filter": "filtro", "flare": "destello", "rain": "lluvia", "anti": "anti", "aliasing": "aliasing",
    "vignetting": "vineteado", "mob": "extras", "face": "cara", "plane": "plano", "refer": "referencia", "staff": "staff",
    "shirt": "camiseta", "facial": "facial", "update": "actualizar", "status": "estado", "vfx": "VFX", "keep": "mantener",
    "create": "crear", "initialize": "inicializar", "allocation": "reserva", "geom": "geometria", "geometry": "geometria",
    "active": "activo", "enter": "entrada", "stay": "permanencia", "exit": "salida", "game": "juego", "dataset": "dataset",
    "enables": "activa", "did": "hizo", "state": "estado", "unique": "unico", "spot": "foco", "cloth": "ropa",
    "bounding": "envolvente", "atmosphere": "atmosfera", "only": "solo", "amount": "cantidad", "clear": "despejado",
    "nodes": "nodos", "edges": "aristas", "prev": "anterior", "next": "siguiente", "inlinks": "enlaces de entrada",
    "outlinks": "enlaces de salida", "members": "miembros", "delete": "borrar", "from": "desde", "steps": "pasos",
    "directions": "direcciones", "sharpness": "nitidez", "horizon": "horizonte", "srm": "SRM", "fmtr": "fmtr",
    "resident": "residente", "search": "busqueda", "subscribers": "suscriptores", "boxes": "cajas", "occluder": "oclusor",
    "one": "una", "positions": "posiciones", "unit": "unidad", "load": "carga", "with": "con", "each": "cada",
    "other": "otro", "parallel": "paralelo", "job": "job", "manager": "gestor", "bodies": "cuerpos",
    "constraints": "restricciones", "polys": "poligonos", "air": "aire", "resistance": "resistencia", "current": "actual",
    "parallax": "paralaje", "probes": "sondas", "loss": "perdida", "manual": "manual", "dash": "pique", "valid": "valido",
    "animation": "animacion", "alias": "alias", "common": "comun", "spans": "tramos", "advance": "avance",
    "global": "global", "preview": "vista previa", "slot": "franja", "pk": "penales", "hour": "hora", "minute": "minuto",
    "second": "segundo", "albedo": "albedo", "inverse": "inverso", "day": "dia", "night": "noche",
    "coefficients": "coeficientes", "open": "apertura", "weather": "clima", "datas": "datos", "rejection": "rechazo",
    "levels": "niveles", "st": "", "nd": "", "ex": "alargue", "gainon": "ganancia", "gain": "ganancia", "x": "X", "y": "Y", "z": "Z",
    "stiffness": "rigidez", "swing": "balanceo", "cone": "cono", "inertia": "inercia", "solver": "solver",
    "iteration": "iteracion", "wind": "viento", "fov": "FOV", "roughness": "rugosidad", "lpsh": "lpsh",
})


def words(n):
    n = n.replace("_", " ")
    return re.findall(r"[A-Z]?[a-z]+|[A-Z]+(?![a-z])|\d+", n)


def fallback(prop, typ):
    w = [x.lower() for x in words(prop)]
    tr = [TOK.get(x, x) for x in w]
    tr = [x for x in tr if x]
    txt = " ".join(tr) if tr else prop
    if typ == "Bool":
        return "Bandera: %s (por nombre)." % txt
    if typ in ("Path",):
        return "Ruta de archivo: %s (por nombre)." % txt
    if typ == "FilePtr":
        return "Archivo cargado: %s (por nombre)." % txt
    if typ in ("EntityPtr", "EntityHandle", "EntityLink"):
        return "Referencia a entidad: %s (por nombre)." % txt
    if typ == "embed":
        return "Struct embebido (sus campos van abajo con prefijo)."
    if typ in ("String",):
        return "Texto: %s (por nombre)." % txt
    return "%s (por nombre)." % (txt[:1].upper() + txt[1:])


def meaning(cls, prop, typ):
    k = "%s.%s" % (cls, prop)
    if k in GLOS_CLASE:
        return GLOS_CLASE[k]
    if prop in GLOS:
        return GLOS[prop]
    # prefijos de TimeOfDaySettings: gameX / demoX
    m = re.match(r"^(game|demo)([A-Z]\w+)$", prop)
    if m:
        base = m.group(2)[0].lower() + m.group(2)[1:]
        if base in GLOS:
            return GLOS[base][:-1] + (" (juego)." if m.group(1) == "game" else " (repeticiones/demos).")
    m = re.match(r"^(default|firstHalf|secondHalf|exFirstHalf|exSecondHalf|pk|end)(Hour|Minute|Second|SunLightMode)$", prop)
    if m:
        tramo = {"default": "previa/por defecto", "firstHalf": "1.er tiempo", "secondHalf": "2.o tiempo",
                 "exFirstHalf": "1.er tiempo del alargue", "exSecondHalf": "2.o tiempo del alargue", "pk": "penales",
                 "end": "final del partido"}[m.group(1)]
        que = {"Hour": "Hora", "Minute": "Minuto", "Second": "Segundo", "SunLightMode": "Modo de luz solar (enum)"}[m.group(2)]
        return "%s del tramo %s." % (que, tramo)
    m = re.match(r"^(\w+?)_(\w+)$", prop)
    if m and m.group(2) in GLOS:
        return "%s (campo de %s)." % (GLOS[m.group(2)][:-1], m.group(1))
    m = re.match(r"^(withers|srm|emissive|pitch)Param(?:eter)?(\d)$", prop)
    if m:
        d = {"withers": "marchitado/pasto seco", "srm": "SRM (especular/rugosidad/metal)", "emissive": "emisivo",
             "pitch": "generico del cesped"}[m.group(1)]
        return "Parametro %s del shader de %s (sentido exacto sin confirmar)." % (m.group(2), d)
    m = re.match(r"^(low|medium|high)(OutputPixelSize|ClusterSize|SamplesPerCluster|RadiosityBudget|ProbeRadiosityBudget|"
                 r"RcFile|IwFile|CawFile|RntFile|VisFile|PuvFile|PatFile)$", prop)
    if m:
        q = {"low": "baja", "medium": "media", "high": "alta"}[m.group(1)]
        d = {"OutputPixelSize": "tamano del pixel de salida de la GI (m por texel de lightmap)",
             "ClusterSize": "tamano de cluster de Enlighten", "SamplesPerCluster": "muestras por cluster",
             "RadiosityBudget": "presupuesto de radiosidad por frame", "ProbeRadiosityBudget": "presupuesto de radiosidad de sondas",
             }.get(m.group(2), "archivo precalculado de Enlighten .%s" % m.group(2)[:-4].lower())
        return "%s%s para calidad %s." % (d[0].upper(), d[1:], q)
    m = re.match(r"^innerScale([XYZ])(Positive|Negative)$", prop)
    if m:
        return "Escala del volumen interior en %s%s (zona de influencia plena de la sonda)." % (
            "+" if m.group(2) == "Positive" else "-", m.group(1))
    m = re.match(r"^simp(\w+)$", prop)
    if m:
        return "Parametro de simplificacion de malla de Enlighten: %s (por nombre)." % " ".join(
            TOK.get(x.lower(), x.lower()) for x in words(m.group(1)))
    m = re.match(r"^(is|enable|use|has|show)([A-Z]\w*)$", prop)
    if m and typ == "Bool":
        return "Bandera: %s %s (por nombre)." % ({"is": "es/esta", "enable": "activa", "use": "usa", "has": "tiene",
                                                  "show": "muestra"}[m.group(1)],
                                                 " ".join(TOK.get(x.lower(), x.lower()) for x in words(m.group(2))))
    return fallback(prop, typ)


def main():
    rows = list(csv.DictReader(open(TSV), delimiter="\t"))
    by = defaultdict(list)
    meta = {}
    for r in rows:
        by[r["class"]].append(r)
        meta.setdefault(r["class"], r)
    cats = defaultdict(list)
    for c in by:
        cats[cat_of(c)].append(c)
    o = []
    w = o.append
    w("# Reflexion de Fox Engine en PES2021.exe 1.07.02: todas las propiedades\n")
    w("Generado por `tools/re/reflect.py` (volcado + desensamblado, emulacion de registros) y `tools/re/reflect_doc.py`. "
      "Base 0x140000000. Datos: `/home/claude/re/out/reflexion.tsv` (%d filas, %d clases), `reflexion_clases.tsv`, "
      "`reflexion_enums.tsv`.\n" % (len(rows), len(by)))
    w("{{FORMATO}}\n")
    w("## Indice por categoria\n")
    for k, desc in CATS.items():
        if cats.get(k):
            n = sum(len(by[c]) for c in cats[k])
            w("- **%s** (%s): %d clases, %d propiedades. %s" % (k, desc, len(cats[k]), n,
                                                              ", ".join("`%s`" % c for c in sorted(cats[k])[:60])
                                                              + (" ..." if len(cats[k]) > 60 else "")))
    w("")
    for k, desc in CATS.items():
        if not cats.get(k):
            continue
        w("\n---\n\n## Categoria `%s`: %s\n" % (k, desc))
        for c in sorted(cats[k], key=lambda x: (-len(by[x]) if k not in ("sistema",) else 0, x)):
            m = meta[c]
            w("### %s\n" % c)
            info = "tamano %s, super `%s`, modulo `%s`, registro `0x%s`" % (m["class_size"] or "?", m["super"] or "-",
                                                                          m["category"] or "-", m["regfn"] or "?")
            w("%s%s.\n" % ((CLASS_DESC[c] + " ") if c in CLASS_DESC else "", info))
            w("| Offset | Propiedad | Tipo | Cont./N | Lua | Significado |")
            w("|---|---|---|---|---|---|")
            for r in by[c]:
                cont = r["container"] if r["container"] != "StaticArray" else ""
                if r["arraySize"] not in ("1", ""):
                    cont = (cont + " " if cont else "") + "[%s]" % r["arraySize"]
                typ = r["type"]
                if r["enum"]:
                    typ += " enum `%s` (%s)" % (r["enum"], r["enum_values"].replace(",", ", "))
                if r["ptr_class"]:
                    typ += " -> `%s`" % r["ptr_class"]
                off = r["offset"]
                if off == "virtual":
                    off = "virtual"
                elif off:
                    off = "+" + off
                w("| %s | `%s` | %s | %s | %s | %s |" % (off, r["property"], typ, cont, r["mode"] or "-",
                                                         meaning(c, r["property"], r["type"])))
            w("")
    txt = "\n".join(o)
    txt = txt.replace("{{FORMATO}}", FORMATO)
    open(MD, "w").write(txt)
    print("escrito", MD, len(txt), "bytes")


FORMATO = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "reflect_formato.md")).read()

if __name__ == "__main__":
    main()
