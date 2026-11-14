// PES21 FX - (14/11) menu Insert visual: tarjetas con dibujos que se tocan (sol que se arrastra en el cielo, viento que se gira
// sobre la cancha), iconos dibujados, el dibujo del mando con lo que se aprieta y los simbolos de botones estilo PlayStation.
// Solo ImGui (sin core.h): overlay.cpp le pasa los valores. Asi tambien compila fuera del juego para sacar capturas de prueba.
#pragma once
#include <imgui.h>

namespace fx {

// --- iconos dibujados con lineas (no dependen de la fuente de iconos) ---
enum MvIcon { kMvIcoNone = 0, kMvIcoSun, kMvIcoWind };
MvIcon MvIconFromName(const char* name);   // "sol" / "viento"
void MvDrawIcon(ImDrawList* dl, ImVec2 center, float size, MvIcon ico, ImU32 col, float t);   // t = reloj (rayos que giran); -1 = quieto

// --- tarjeta del sol: cielo visto desde arriba (centro = sol arriba de todo, borde = horizonte) con la cancha y la sombra de un
// jugador, y al lado una vista previa de la luz sobre una pelota / cabeza. El sol se arrastra con el mouse. ---
struct MvSunIn {
    float* on = nullptr;          // x101 (se prende solo al arrastrar el sol)
    float* az = nullptr;          // y101 (grados; si 'relative', giro desde el sol real)
    float* el = nullptr;          // z101 (grados, -5..90)
    float intensity = 1.0f;       // w101
    float warm = 0.0f;            // x102
    bool relative = false;        // y450
    bool realOk = false; float realAz = 0, realEl = 0;   // sol del partido
    bool finalOk = false; float finalAz = 0, finalEl = 0; // donde quedo el sol libre este cuadro (giro, recorrido del dia)
    float tint[3] = { 1, 1, 1 };  // fila 453 (temperatura, color segun la altura, nubes); 1,1,1 = sin tinte
    bool anim = true;             // vistas dinamicas con animacion
    float k = 1.0f;               // escala del menu (UIK)
    double now = 0.0;             // reloj en segundos
};
bool MvSunWidget(MvSunIn& in);    // true = cambio algo

// --- tarjeta del viento: la cancha desde arriba con particulas que vuelan con el viento; arrastrar fija el rumbo ---
struct MvWindIn {
    bool live = false;            // hay viento unico corriendo (g_windNow.on)
    float speed = 0, dirDeg = 0, soonSpeed = 0, soonDirDeg = 0, gust = 0, turb = 0;
    float* fixed = nullptr;       // y501 (0 del partido, 1 fijo)
    float* fixedDeg = nullptr;    // z501
    float* strength = nullptr;    // x353 (fuerza general; rueda del mouse sobre la cancha)
    bool anim = true; float k = 1.0f; double now = 0.0;
};
bool MvWindWidget(MvWindIn& in);

// --- mando ---
enum MvPadBtn {
    kPadCross = 0, kPadCircle, kPadSquare, kPadTriangle, kPadL1, kPadR1, kPadL2, kPadR2, kPadShare, kPadOptions,
    kPadL3, kPadR3, kPadPS, kPadTouch, kPadUp, kPadDown, kPadLeft, kPadRight, kPadCount
};
struct MvPad {
    bool connected = false; int source = 0;   // 1 XInput, 2 joystick de Windows (DualShock / DualSense sin Steam Input)
    bool b[kPadCount] = {};
    float lx = 0, ly = 0, rx = 0, ry = 0;     // -1..1, y hacia abajo positiva
    float l2 = 0, r2 = 0;                     // 0..1
};
bool MvPadPoll(MvPad& p);                     // lee el primer mando (solo Windows; afuera devuelve false)
void MvPadFeedImGui(const MvPad& p);          // le pasa el mando a ImGui si el backend no lo hizo (joystick de Windows)
void MvPadGlyph(ImDrawList* dl, ImVec2 c, float r, int btn, float alpha, float lit = 0.0f);   // simbolo del boton (cruz, circulo, ...)
// dibujo del mando con lo apretado y los analogicos; 'where' = esquina de arriba a la izquierda; ancho ~ 230 * k
void MvPadDraw(ImDrawList* dl, ImVec2 where, float k, const MvPad& p, float alpha, bool anim, double now);
ImVec2 MvPadSize(float k);
// fila de ayuda: "X Elegir  O Volver  ..." con los simbolos; devuelve el alto usado
void MvPadHints(float k, float alpha);

} // namespace fx
