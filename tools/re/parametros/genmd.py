import pickle,collections,manual,gloss
D=pickle.load(open('data.pkl','rb')); S=pickle.load(open('stats.pkl','rb'))
use,calls,wrap=pickle.load(open('consumers.pkl','rb'))
CL={
'Ball':'Fisica de la pelota: rebote, friccion de rodadura, efecto (spin), Magnus, arrastre. Muchos campos son arrays de 6: 6 variantes de superficie/estado (el indice exacto no esta confirmado; la [3] y la [4] son claramente distintas: menos rebote y mas friccion, compatible con cancha mojada/pesada).',
'CameraInplay':'Camara del partido en juego: la "Wide" nueva con tres juegos de parametros (paramMin/paramMax segun el ajuste de distancia/altura del menu, relayParam para la transicion), zona muerta y tiempos de movimiento, punto de caida rapido del pase.',
'CameraSetplay':'Camara de tiros libres (fk) y penales (pk): seguimiento de pelota y tiempos de cambio.',
'InplayDemo':'Demo/repeticion en juego: tiempos de mezcla y penalizaciones de area.',
'MlScreenShot':'17 patrones de toma de camara (posicion, mirada, FOV, momento) usados para capturas automaticas por escena (scene 0..5).',
'StudiumPositionData':'Posiciones del estadio para demos: entradas, area tecnica, area medica, carteles, tribunas local/visitante. La base esta en cero; los valores reales estan en las 84 variantes demoarea_* (ver constant_gameplay_variantes.tsv).',
'Cursor':'Cambio de cursor (jugador controlado): distancias y tiempos.',
'CpuLevel':'Dificultad de la CPU: 34 parametros (Info[0..33], sin nombre en el JSON) con un valor por nivel lv1..lv7 y bandera lerp (interpolar). Los nombres de los niveles salen de MATCH_LEVEL_BEGINNER/AMATEUR/REGULAR/PROFESSIONAL/SUPERSTAR/LEGEND (cadenas en TutorialNone.MatchLevel); que es cada Info[i] requiere seguir a los consumidores (lectores virtualizados).',
'Injury':'Umbrales de dano para nivel de lesion y tipo de sintoma (contusion, inflamacion, laceracion, desgarro, ligamentos, fractura).',
'Motivation':'Multiplicadores del efecto (positivo/negativo) de la motivacion sobre las stats.',
'TeamEmotion':'Emocion/animo del equipo: 21 categorias de evento (GOAL, SHOOT, ...) y umbrales de la barra local/visitante.',
'Rating':'Calificacion del jugador al final del partido: puntos extra, coeficientes por accion y las formulas textuales por puesto (df/mf/fw/gk_rate) evaluadas sobre StatsPlayer*().',
'PesSmart':'Ajustes "PES Smart"/UEFA: tiro libre por deslizamiento (tactil/raton), atajabilidad del arquero, presion del arquero, faltas y TODO el modelo de cansancio (stamina.*).',
'Ballplayer':'Datos medidos de giro del jugador con pelota: TurnData = 20 tipos x 72 angulos de entrada (cada 5 grados) = 1440 entradas de 0x34 bytes desde +0x14 (confirmado por FUN_14063e2c0, que indexa kind<0x14 y angulo/5). touch0[] = 7 tipos de toque con frames y largos.',
'FeintCommand':'Tabla de comandos de fintas: 35 entradas (lista de botones/palancas, angulos, tiempos) mapeadas a FEINT_KIND_*.',
'PlayStyle':'Tabla de estilos de juego (462 enteros, todos 0 en el .bin retail).',
'BasePosition':'Posicionamiento base del equipo (lineas, ancho, compactacion, repliegue, balon parado). Es el "cerebro" de la formacion en juego.',
}
def collapse(c):
    g=collections.OrderedDict()
    for r in D[c]['rows']: g.setdefault(r['pat'],[]).append(r)
    out=[]
    for p,rs in g.items():
        vals=[r['vs'] for r in rs]
        vs=('%s (x%d)'%(vals[0],len(vals))) if len(set(vals))==1 and len(vals)>1 else ' '.join(vals)
        if len(vs)>110: vs=vs[:110]+' ...'
        out.append((p,rs[0],len(rs),vs))
    return out
def tipo(r):
    t=r['type']
    if t=='int': return 'i32' if r['size']==4 else ('i%d'%(8*r['size']) if isinstance(r['size'],int) else 'i?')
    if t=='string': return 'string' if r['size']=='str' else 'str->enum'
    return {'float':'f32','bool':'bool'}[t]
L=[]
w=L.append
w('# Parametros de gameplay `constant::*` (PES 2021 1.07.02)\n')
w('Fuente: 84 clases de datos `constant::*` (87 en rtti.txt; 3 son infraestructura: `ArchiveFile`, `ConstantFile`, `ConstantData`). '
  'Para cada una se volco la tabla clave JSON -> offset en el objeto, tipo, valor por defecto (el que trae el juego retail) y significado inferido. '
  'Tabla completa: `out/constant_gameplay.tsv` (%d filas, una por valor). Resumen con arrays colapsados: `out/constant_gameplay_resumen.tsv`. '
  'Valores de las variantes (posiciones de corner/penal, 84 estadios demo, 26 tutoriales de consola, 20 bloques de prueba de tiro): `out/constant_gameplay_variantes.tsv`.\n'%sum(v['n'] for v in S.values()))
w('## 1. Como funciona (evidencia)\n')
w('- Cada clase hereda de `constant::ConstantData` (vtable 0x142b59a38) y tiene 3 virtuales: `vf0` = destructor, **`vf1` = lector JSON**, **`vf2` = lector binario**. Ejemplo modelo: `constant::CameraInplay` vtable 0x142b69598, vf1 0x141e89fc0, vf2 0x141e89d40.')
w('- `vf1(this, json)`: secuencia `FUN_140383d70(nodo,"clave")` (hijo de objeto), `thunk_FUN_140383cc0` @0x140383cb0 (elemento de array) y un lector de hoja: `FUN_140384220` (double -> float32), `FUN_140384320` (int32), `FUN_1403841a0` (bool), `FUN_141f1ff90` (cadena, que se asigna con `FUN_1404023c0` = `std::string::assign`, hueco de 0x20 bytes). Despues escribe en `*(T*)(this+off)`.')
w('- `vf2(this, blob)`: copia campos de un blob binario; los objetos anidados y arrays son offsets u32 relativos al blob (`FUN_141e8af70(off, base)` = `off ? base+off : 0`).')
w('- Tabla de las 234 constantes (id -> ruta JSON, fabrica): 0x142b54d80, 16 bytes por entrada; `FUN_141e669c0(id)` la indexa y `FUN_141e669e0()` devuelve 0xea. '
  'El administrador global es `DAT_143802c88`; `FUN_141e65580(DAT_143802c88, id)` (230 llamadores) y `FUN_141e66020(ctx, id)` (148) devuelven el objeto de datos para el id. '
  '`constant::ConstantFile::vf1` (0x141e66ae0) parsea el JSON, llama a la fabrica y a `vf1`, y registra con `FUN_141e653d0`. `constant::ArchiveFile::vf1` (0x141e660c0) recorre las entradas `.o` de un `.bin` y las registra con `FUN_141e656a0`.')
w('- Rutas: el JSON de desarrollo es `DevelopData/common/match/constant/<grupo>/<nombre>.json` (cadenas 0x142b55c20..); el retail carga `cpk_dat/common/match/constant/constant_{match,player,stadium,team,tutorial,tutorialConsole,positionCK,positionPK,shootAging}.bin` (cadenas 0x14259c910.., puntero en 0x143453e70..). Los `.bin` estan en `dt18_all.cpk` (extraidos en `/home/claude/cpk/dt18_all/common/match/constant/`). Los 7 `tutorialMobile/*` (ids 227-233) no tienen `.bin` en ese cpk.\n')
w('### Como se obtuvieron offsets y valores\n')
w('1. Se **emularon** con Unicorn (sobre el volcado, sin Ghidra) los 84 `vf1` con un JSON simulado: cada lectura devuelve un marcador unico y un gancho de escritura registra donde termina (offset, tamano). Asi salen clave -> offset -> tipo para todas las clases, incluso las que tienen arrays y bucles. Ningun valor se transforma al cargarse (no hay escalas tipo grados->radianes).')
w('2. Se emularon los `vf2` con el blob real de cada `.o`: 38 clases tienen codigo nativo (valores "emulado" en el TSV). Los otros 46 `vf2` estan **virtualizados** por la proteccion (saltan a 0x15aXXXXXX, fuera del volcado).')
w('3. Para esas 46 se uso el modelo de disposicion del blob deducido de las 38: los campos van en el orden del JSON con alineacion natural (bool 1 byte, int/float 4), los objetos anidados y arrays ocupan un u32 con el offset a su contenido, los elementos de un array de objetos van contiguos, las cadenas son un u32 al texto. **El modelo coincide en 23.477 de 23.481 valores** de las 38 clases verificadas (las 4 diferencias son las formulas largas de Rating, por un limite de lectura del verificador, no del modelo). Columna `valor_verificado` = `emulado` o `modelo`.\n')
w('### Formato para editar los valores (modding)\n')
w('- `constant_*.bin` = cabecera `FF 10 "QWESYS"` + u32 tamano comprimido + u32 tamano descomprimido + zlib.')
w('- Descomprimido: u32 cantidad, u32 (8), luego por entrada `{u32 offset_datos, u32 tamano, u32 offset_nombre}`; nombres `xxx.o` en ASCII.')
w('- Columna `offset_en_bin_descomprimido` del TSV = posicion exacta del valor dentro del `.bin` descomprimido (offset de la entrada + `offset_en_o`). Cambiar ese float/int, recomprimir con zlib y rehacer la cabecera WESYS. (No probado en el juego: solo lectura.)')
w('- `offset` = posicion del campo dentro del objeto en memoria (el objeto empieza con la vtable en +0). Con `FUN_141e65580(DAT_143802c88, id)` se obtiene el puntero al objeto vivo, util para un parche en memoria.\n')
w('### Rarezas encontradas\n')
w('- `BasePosition.transitionSmooth` y `Block.press.checkArea` se leen como int pero el `.bin` trae bits de float (0x3f800000 = 1.0, 0x40000000 = 2.0): el juego retail usa 1065353216 y 1073741824. Probable error de datos de Konami.')
w('- Varias claves tienen typos originales: `gounnderprob`, `natualBoundA`, `natunalRollBackSpinRate`, `rigth`, `eneble`, `recieve`, `stratagy`, `AudiencFixationPoint`, `limitiTime`.')
w('- Muchas clases `Ballplayer*` (Analyze, Clear, Dribble, Feint, Pass, PlayImage, Shoot) solo tienen `dummy`: el contenido real se movio a otras clases.\n')
w('## 2. Indice de clases\n')
w('| clase | tema | vtable | vf1 JSON | vf2 bin | id | JSON | .bin / .o | campos | verif. |')
w('|---|---|---|---|---|---|---|---|---|---|')
topic_of={}
for t,cs in manual.TOPIC.items():
    for c in cs: topic_of.setdefault(c,t)
for c,v in S.items():
    w('| %s | %s | 0x%s | 0x%s | 0x%s | %s | `%s` | %s / %s | %d | %s |'%(c,topic_of.get(c,'?'),v['vtable'],v['vt'][2],v['vt'][3],v['id'],v['json'].split('constant/')[1] if v['json'] else '',v['arch'],v['o'],v['n'],'emulado' if v['verified'] else 'modelo'))
w('')
w('## 3. Por tema\n')
w('Formato: `clave` (offset en objeto, tipo) = valor por defecto -- significado. Los arrays se muestran como `clave[]` con la lista de valores. "por nombre" = traduccion de la clave, sin confirmacion de uso.\n')
n=3
for t,cs in manual.TOPIC.items():
    w('### %s\n'%t)
    for c in cs:
        if c not in S: continue
        v=S[c]
        w('#### `constant::%s`\n'%c)
        w('JSON `%s` - retail `%s` / `%s` - id %s - vf1 0x%s, vf2 0x%s - %d valores (%s).\n'%(v['json'],v['arch'],v['o'],v['id'],v['vt'][2],v['vt'][3],v['n'],'emulado' if v['verified'] else 'modelo validado'))
        if c in CL: w(CL[c]+'\n')
        if t=='Condicion fisica / stamina' and c=='PesSmart':
            rows=[x for x in collapse(c) if x[0].startswith('stamina')]
        elif t=='IA de equipo (tactica, desmarques, lineas)' and c=='PesSmart':
            w('(Las claves `stamina.*` se listan en "Condicion fisica / stamina".)\n'); rows=[x for x in collapse(c) if not x[0].startswith('stamina')]
        else: rows=collapse(c)
        w('| clave | off | tipo | default | significado |'); w('|---|---|---|---|---|')
        for p,r,k,vs in rows:
            m=manual.M.get((c,p)) or ('por nombre: '+gloss.gloss(r['path']))
            w('| `%s`%s | 0x%x | %s | %s | %s |'%(p,' x%d'%k if k>1 else '',r['off'],tipo(r),vs.replace('|','/'),m.replace('|','/')))
        u=use.get(v['id'],{}) if v['id']!='' else {}
        cs_=sorted(calls.get(v['id'],[])) if v['id']!='' else []
        if cs_:
            w('\nConsumidores encontrados en el C (llaman al getter con id %s): %s%s'%(v['id'],', '.join('`%s`'%x for x in cs_[:8]),' ...' if len(cs_)>8 else ''))
            offs=sorted(u)
            if offs: w('; offsets leidos: %s.'%', '.join('0x%x'%o for o in offs[:30]))
        w('')
w('### Arbitro y clima\n')
w('No hay una clase `constant::*` dedicada al arbitro ni al clima. Lo relacionado que si esta en estos datos: `PesSmart.foul.*` (ventana y area de la accion de falta), `Defence.continueAdvantage*` (como sigue la defensa durante la ley de ventaja), lineas de offside en `SpaceRun`, `DiagonalRun`, `Combination`, `BasePosition.backOffsideLine`, `UserPlayTendencyTest.offSidetrap`; `TutorialNone.isFoulEnd`. '
  'Clima/cancha: `Ball.pitchCondition` (=1; enum PLAY_DATA_PITCH_CONDITION_DRY/NORMAL/WET en 0x142b4dd18..) y los arrays de 6 de `Ball` (friccion/rebote por variante de superficie). La lluvia (RAIN_OFF/RAIN_SCREEN/RAIN_DEMO, WEATHER_*) se maneja fuera de `constant::*` (ver informes de render/TOD).\n')
w('## 4. Limitaciones\n')
w('- Los 46 lectores binarios virtualizados no se pudieron ejecutar; sus valores salen del modelo (validado al 99,98 %).')
w('- Los significados son inferidos por nombre salvo donde se cita codigo (TurnData de Ballplayer, cadenas de enums). El indice de los arrays de 6 de `Ball` y que es cada `CpuLevel.Info[i]` no estan confirmados.')
w('- "Consumidores" sale de un barrido por regex del C (variable asignada desde el getter y luego `(var + off)`); muchos consumidores reales pasan el puntero por otras variables o estan virtualizados, asi que la lista es parcial.')
w('- No hay valores por defecto en el exe (las fabricas tambien estan virtualizadas): el "default" es el valor del `.bin` retail (dt18_all.cpk). Un mod que reemplace el `.bin` cambia esos valores.')
open('/home/claude/re/out/constant_gameplay.md','w').write('\n'.join(L)+'\n')
print(len(L))
