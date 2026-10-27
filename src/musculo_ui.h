// PES21 FX - ventana de la musculatura (26/10 c): diseno propio (mapa del cuerpo, pestanas por zona, barras con el valor del
// juego marcado). Solo usa ImGui: los datos los da musculo.cpp con un MusUiModel (asi se puede probar sin el juego).
#pragma once
#include <cstdint>
#include <string>
struct ImFont;

namespace fx {

struct MusUiPlayer { uint32_t id = 0; std::string name; bool own = false; };

class MusUiModel {
public:
    virtual ~MusUiModel() {}
    // jugadores (en el menu Fisico del juego hay uno solo: el que se edita)
    virtual int PlayerCount() = 0;
    virtual const MusUiPlayer& Player(int i) = 0;
    virtual int Selected() = 0;
    virtual void Select(int i) = 0;
    // valores del jugador elegido (si no tiene propios: los generales; Set le crea valores propios)
    virtual bool HasOwn() = 0;
    virtual void MakeOwn() = 0;
    virtual void ClearOwn() = 0;
    virtual void CopyGeneral() = 0;
    virtual float Get(int idx, int comp) = 0;
    virtual float General(int idx, int comp) = 0;
    virtual void Set(int idx, int comp, float v) = 0;
    // opciones de todos (interruptor, musculos vivos)
    virtual bool MuscleOn() = 0;
    virtual void SetMuscleOn(bool on) = 0;
    virtual float Global(int idx, int comp) = 0;
    virtual void SetGlobal(int idx, int comp, float v) = 0;
    // tipos de cuerpo (los del mod primero, despues los propios)
    virtual int TypeCount() = 0;
    virtual int BuiltinTypes() = 0;
    virtual const char* TypeName(int k) = 0;
    virtual const char* TypeDesc(int k) = 0;
    virtual int TypeMatch() = 0;                     // -1 = a mano
    virtual void ApplyType(int k) = 0;
    virtual std::string SaveAsType(const char* name) = 0;
    virtual void DeleteUserType(int k) = 0;          // k = indice en la lista completa
    // otros jugadores con valores propios (para copiar)
    virtual int OtherCount() = 0;
    virtual const char* OtherName(int i) = 0;
    virtual void CopyFromOther(int i) = 0;
    // en el juego: el musculo bajo el mouse late en el modelo; mantener "Comparar" = el cuerpo como el juego
    virtual void Highlight(int idx, int comp) = 0;
    virtual void Compare(bool hold) = 0;
    virtual std::string& Status() = 0;
};

// dibuja la ventana entera dentro de la ventana / tarjeta actual de ImGui
void MuscleUiDraw(MusUiModel& m, ImFont* bold, ImFont* big);

} // namespace fx
