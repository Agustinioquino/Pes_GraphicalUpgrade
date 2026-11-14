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
    // (14/11 c) las demas opciones de la tarjeta, para tocarlas con botones de icono (nullptr = no se muestra)
    float* intensityP = nullptr;  // w101
    float* warmP = nullptr;       // x102
    float* follow = nullptr;      // x450 sombras que siguen al sol
    float* kelvin = nullptr;      // z450 temperatura (0 = juego)
    float* spin = nullptr;        // w450 giro automatico grados/s
    float* copy = nullptr;        // x451 copiar el sol del partido
    float* heightCol = nullptr;   // x455 color segun la altura
    float* dayRun = nullptr;      // y455 recorrido del dia
    float* clouds = nullptr;      // z455 nubes que pasan
    bool anim = true;             // vistas dinamicas con animacion
    float k = 1.0f;               // escala del menu (UIK)
    double now = 0.0;             // reloj en segundos
};
bool MvSunWidget(MvSunIn& in);    // true = cambio algo

// --- tarjeta del viento (14/11 e, como la del sol): preajustes de un clic, dial con la cancha (arrastrar = rumbo fijo), vista de
// costado con el banderin (arrastrar la barra = fuerza general) y botones con dibujo para lo demas ---
struct MvWindIn {
    bool live = false;            // hay viento unico corriendo (g_windNow.on)
    float speed = 0, dirDeg = 0, soonSpeed = 0, soonDirDeg = 0, gust = 0, turb = 0;
    float* fixed = nullptr;       // y501 (0 del partido, 1 fijo)
    float* fixedDeg = nullptr;    // z501
    float* strength = nullptr;    // x353 (fuerza general)
    float* preset = nullptr;      // x501 (0 partido, 1 calmo, 2 brisa, 3 ventoso, 4 tormenta)
    float* change = nullptr;      // w501 cambio durante el partido
    float* gusts = nullptr;       // x502 rachas
    float* gustLen = nullptr;     // y502 largo de las rachas
    float* turbP = nullptr;       // z502 turbulencia
    float* veer = nullptr;        // w502 giro del rumbo
    float* shelterMode = nullptr; // x503 (0 segun el estadio, 1 a mano)
    float* shelter = nullptr;     // y503 reparo a mano
    float* grass = nullptr;       // z503 viento en el pasto
    float* cloth = nullptr;       // w503 viento en la ropa
    bool anim = true; float k = 1.0f; double now = 0.0;
};
bool MvWindWidget(MvWindIn& in);

// --- tarjetas de la camara (14/11, Agustin: "mas diseno para las opciones de camara libre") ---

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
// estilo del dibujo y de los simbolos: PS5, PS4 o Xbox (como los iconos de botones que muestra el juego)
enum MvPadStyle { kPadStylePS5 = 0, kPadStylePS4, kPadStyleXbox };
void MvPadSetStyle(int style, unsigned rgb = 0);   // rgb 0xRRGGBB del cuerpo; 0 = el de cada estilo
int MvPadGetStyle();
int MvPadDetectStyle(const wchar_t* gameDir);    // mira livecpk\Dream Patch 21 Graphic Controller (PS5 si no esta)
bool MvPadPoll(MvPad& p);                     // lee el primer mando (solo Windows; afuera devuelve false)
void MvPadFeedImGui(const MvPad& p);          // le pasa el mando a ImGui si el backend no lo hizo (joystick de Windows)
void MvPadGlyph(ImDrawList* dl, ImVec2 c, float r, int btn, float alpha, float lit = 0.0f);   // simbolo del boton (cruz, circulo, ...)
// dibujo del mando con lo apretado y los analogicos; 'where' = esquina de arriba a la izquierda; ancho ~ 230 * k
void MvPadDraw(ImDrawList* dl, ImVec2 where, float k, const MvPad& p, float alpha, bool anim, double now);
ImVec2 MvPadSize(float k);
// fila de ayuda: "X Elegir  O Volver  ..." con los simbolos; devuelve el alto usado
void MvPadHints(float k, float alpha);
struct MvCamIn {
    float* mode = nullptr;      // x344 0 apagada, 1 prendida, 2 sin bloquear el teclado, 3 solo en las repeticiones
    float* speed = nullptr;     // y344 m/s
    float* zoom = nullptr;      // z344 (x el campo de vision del juego)
    float* sens = nullptr;      // w344 mouse
    float* follow = nullptr;    // x346 0 no, 1 fija al rostro, 2 orbitar
    float* smooth = nullptr;    // y346
    bool active = false, replay = false;   // la camara libre esta volando ahora / hay repeticion
    bool keys[8] = {};          // W A S D Q E Shift Ctrl apretadas (para iluminar el dibujo)
    bool rmb = false;           // boton derecho del mouse
    MvPad pad;                  // (14/11) el mando de este cuadro: los iconos se iluminan con lo que se aprieta
    int* padOn = nullptr;       // camara_mando (0 / 1)
    bool padFly = false;        // vuela con el mando ahora
    bool padGiven = false;      // OPTIONS: el mando se le devolvio al juego
    bool padUi = false;         // lo ultimo que se toco fue el mando: se muestran sus controles (si no, teclado y mouse)
    bool anim = true; float k = 1.0f; double now = 0.0;
};
bool MvCamWidget(MvCamIn& in);
struct MvSlowIn {
    float* on = nullptr;        // y343
    float* speed = nullptr;     // x343
    float* where = nullptr;     // z343 0 editor, 1 partido y repeticiones, 2 solo repeticiones
    float editorSteps = 0;      // w343 (pasos frenados del visor por segundo)
    bool replay = false;
    MvPad pad; bool padFly = false, padUi = false;         // (14/11) modo foto con el mando; padUi = lo ultimo tocado fue el mando
    const float* steps = nullptr; int nSteps = 0;          // pasos del modo foto
    int photoStep = -1; bool photoPaused = false, photoAdv = false; float photoScale = -1.0f;
    bool anim = true; float k = 1.0f; double now = 0.0;
};
bool MvSlowWidget(MvSlowIn& in);
// (14/11, Agustin: "una interfaz o hud adaptable, y mostrar lo mas posible de la pantalla del propio juego") HUD DE LA CAMARA
// LIBRE / MODO FOTO: una barra fina abajo (estado, velocidad, zoom, reloj con los pasos y los botones con su funcion); los botones
// que no entran se sacan; sin tocar nada se achica a una pastilla en la esquina (compact 0..1); hidden = no se dibuja.
struct MvHudIn {
    MvPad pad; bool padMode = false;            // volando con el mando: simbolos del mando; si no, teclas
    bool freeCam = false; const char* status = "";   // "EN VIVO" / "REPETICION"
    float speed = 1.0f, zoom = 1.0f; bool follow = false;
    float clock = 1.0f; bool photo = false, paused = false, adv = false; int step = -1; const float* steps = nullptr; int nSteps = 0;
    float compact = 0.0f;                       // 0 barra completa, 1 pastilla
    float k = 1.0f, alpha = 1.0f; bool anim = true; double now = 0.0;
};
void MvPhotoHud(ImDrawList* dl, ImVec2 scrMin, ImVec2 scrMax, const MvHudIn& in);
// (14/11) teclado en pantalla para escribir con el mando (buscador del menu). Cruceta / stick mueven, cruz escribe, cuadrado borra,
// triangulo = espacio, L1 borra todo, circulo u OPTIONS cierran. Tambien con el mouse.
// (14/11) cursor propio del menu: flecha con sombra y filo de luz; sobre algo que se toca se pinta con el verde del menu y crece un poco,
// al hacer clic sale una onda; sobre un campo de texto es una barra I. kind: 0 flecha, 1 sobre algo que se toca, 2 texto
void MvCursorDraw(ImDrawList* dl, ImVec2 pos, int kind, bool down, float k, float alpha, bool anim);
void MvVkOpen();
void MvVkClose();
bool MvVkIsOpen();
// dibuja y maneja el teclado (llamar una vez por cuadro despues del menu); escribe en buf (UTF-8). Devuelve true si cambio el texto
bool MvVkVisible();   // abierto o terminando de desvanecerse
// backdrop: el fondo de vidrio del menu (overlay.cpp GlassBackdrop); nullptr = panel oscuro
bool MvVirtualKeyboard(char* buf, int cap, const MvPad& p, float k, float alpha, bool anim, void (*backdrop)(ImVec2, ImVec2, float, float) = nullptr);

} // namespace fx
