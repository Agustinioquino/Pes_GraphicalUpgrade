import sys
D = {
 "sub":"suplente","goal":"gol","celebrate":"festejo","md":"partido","end":"final","ent":"entrada","cup":"copa","mobH":"gente","cmn":"comun",
 "foul":"falta","lg":"liga","news":"noticia","walk":"caminar","card":"tarjeta","trophy":"trofeo","hi":"saludo","timeup":"fin del tiempo",
 "meeting":"charla","kickoff":"saque inicial","hugSet":"abrazo","pk":"penal","idle":"quieto","entering":"entrando","win":"gana","tu":"otros",
 "champione":"campeon","home":"local","prop":"utileria","uefa":"UEFA","judge":"arbitro decide","aisle":"pasillo","anth":"himno","myclub":"myClub",
 "gadget":"aparato","gk":"arquero","photo":"foto","away":"visitante","triumph":"triunfo","turn":"giro","fp":"jugador de campo","mob":"gente",
 "audi":"tribuna","sit":"sentado","overBoard":"sobre el cartel","ball":"pelota","spectator":"espectador","transfer":"traspaso","cam":"camara",
 "mission":"mision","cardpack":"sobre de cartas","change":"cambio","joy":"alegria","pair":"pareja","visit":"visita","move":"moverse",
 "announce":"anuncio","tvcam":"camara de TV","loop":"en bucle","stand":"parado","Golazo":"golazo","golazo":"golazo","hug":"abrazo",
 "handshake":"apreton de manos","positive":"positivo","stretch":"elongar","success":"exito","acquisition":"fichaje","man":"hombre",
 "glad":"contento","yell":"grito","run":"correr","pinch":"apuro","lockerSpeech":"charla en el vestuario","coop":"juntos","neutral":"neutral",
 "messi":"Messi","lost":"perdio","handover":"entrega","lose":"pierde","excite":"emocion","speech":"discurso","report":"informe","camera":"camara",
 "wait":"espera","waiting":"esperando","injury":"lesion","coach":"tecnico","raise":"levanta","locker":"vestuario","in":"entra","out":"sale",
 "smartphone":"celular","referee":"arbitro","mvp":"MVP","negative":"negativo","neg":"negativo","tvcamera":"camara de TV","clap":"aplauso",
 "stairUp":"sube escalera","award":"premio","awards":"premios","pickup":"levantar","lawn":"cesped","pickBall":"agarra la pelota","pen":"penal",
 "question":"pregunta","fence":"valla","ready":"listo","jog":"trote","jogging":"trote","noCard":"sin tarjeta","touch":"toque","circle":"ronda",
 "right":"derecha","talk":"charla","oncoming":"acercandose","failure":"fallo","Failure":"fallo","paper":"papel","note":"nota","ally":"companero",
 "menu":"menu","set":"conjunto","backhand":"reves","busthand":"palmada","joinhand":"manos juntas","reply":"respuesta","answer":"respuesta",
 "center":"centro","conference":"conferencia","rival":"rival","miss":"erra","normal":"normal","interview":"entrevista","kick":"patada",
 "fouled":"le hacen falta","flagbearer":"abanderado","ovation":"ovacion","board":"cartel","warmup":"precalentamiento","warmupHandclap":"precalentamiento con aplauso",
 "beJudged":"lo sancionan","lift":"levantar","armfolding":"brazos cruzados","postmatch":"despues del partido","lockertalk":"charla en el vestuario",
 "derby":"clasico","guide":"guia","kneelRise":"se levanta de rodillas","pounce":"se tira encima","cupstand":"podio de la copa","sadness":"tristeza",
 "notouch":"sin tocar","circlejump":"ronda saltando","highfive":"choca los cinco","receiver":"receptor","indicate":"senala","protest":"protesta",
 "standup":"se para","practice":"practica","player":"jugador","greeting":"saludo","aghast":"atonito","double":"doble","show":"muestra",
 "dontmind":"no pasa nada","fatigue":"cansancio","extra":"alargue","ownGoal":"gol en contra","tablet":"tablet","dribble":"gambeta","catch":"atrapa",
 "lockercelebrations":"festejo en el vestuario","leave":"se va","manager":"tecnico","teamflag":"bandera del equipo","carry":"lleva",
 "offsideGoal":"gol en offside","shoot":"remate","hattrick":"hat-trick","assistant":"juez de linea","flag":"bandera","up":"arriba","offside":"offside",
 "greatEffort":"gran esfuerzo","joyHigh":"mucha alegria","rideShoulder":"en andas","handclap":"aplauso","respond":"responde","callAid":"pide asistencia",
 "sitdown":"se sienta","mannequin":"maniqui","block":"bloqueo","injurysign":"senal de lesion","highest":"maximo","sign":"senal","greatGlad":"muy contento",
 "kicker":"pateador","anger":"enojo","substitute":"suplente","supersub":"suplente estrella","bothhand":"dos manos","onehand":"una mano",
 "disappointed":"decepcion","vexing":"bronca","straight":"derecho","slant":"diagonal","stepping":"pasos","warn":"advertencia","cornerflag":"banderin",
 "item":"objeto","boots":"botines","trap":"control","deflect":"desvio","lead":"ventaja","frustration":"frustracion","avoid":"esquiva","chairmove":"mueve la silla",
 "cross":"centro","sidestepL":"paso al costado","sidewalkR":"camina de costado","cool":"tranquilo","cheerUp":"da animo","keeper":"arquero",
 "firstkeeper":"primer arquero","firstkicker":"primer pateador","open":"abre","undershirt":"camiseta interior","sad":"triste","far":"lejos",
 "middle":"medio","near":"cerca","ace":"figura","promote":"ascenso","glanceSet":"mirada","backstep":"paso atras","heading":"cabezazo","hopping":"saltitos",
 "jump":"salto","stepjump":"salto con paso","turnstep":"paso con giro","waist":"cintura","swing":"balanceo","arm":"brazo","twist":"giro",
 "tie":"empate","slideFaceup":"se desliza boca arriba","idleHip":"quieto con manos en la cintura","talkSet":"charla","enteringStairDown":"baja la escalera",
 "rideGoal":"sobre el arco","console":"consuela","combi":"combinacion","guts":"garra","icc":"ICC","gunmic":"microfono","judgeFlagBoard":"cartel de cambios",
 "1st":"primero","2nd":"segundo","row":"fila","full":"completo","group":"grupo","pos":"posicion","tube":"tunel","circleJPN":"ronda","y":"","A":"A","B":"B","C":"C",
}
def esc(x):
    # MSVC: un \xNN seguido de un digito hexadecimal rompe la cadena -> se corta el literal ("..." "...")
    o=''; prevEsc=False
    for ch in x.encode('utf-8'):
        if 32<=ch<127 and ch not in (34,92):
            c=chr(ch)
            if prevEsc and c in '0123456789abcdefABCDEF': o+='" "'
            o+=c; prevEsc=False
        else:
            o+='\\x%02X'%ch; prevEsc=True
    return o
tr={"camara":"cámara","comun":"común","noticia":"noticia","campeon":"campeón","arbitro":"árbitro","mision":"misión","alegria":"alegría","apreton":"apretón",
    "exito":"éxito","lesion":"lesión","tecnico":"técnico","cesped":"césped","senala":"señala","senal":"señal","companero":"compañero","reves":"revés",
    "ovacion":"ovación","clasico":"clásico","guia":"guía","practica":"práctica","atonito":"atónito","frustracion":"frustración","decepcion":"decepción",
    "advertencia":"advertencia","banderin":"banderín","combinacion":"combinación","emocion":"emoción","maniqui":"maniquí","maximo":"máximo","linea":"línea",
    "utileria":"utilería","posicion":"posición","tunel":"túnel","atras":"atrás","menu":"menú","garra":"garra","microfono":"micrófono","rodillas":"rodillas",
    "gambeta":"gambeta","acercandose":"acercándose","perdio":"perdió","esta":"está","consuela":"consuela","senal de lesion":"señal de lesión"}
def es(v):
    w=v.split(' ')
    w=[tr.get(x,x) for x in w]
    return ' '.join(w)
rows=sorted(D.items())
out=['// generado por tools/motor/motorclipdesc_gen.py: partes del nombre de los clips -> castellano (Motor, 17/11 18:01)',
     'struct ClipWord { const char* k; const char* es; };','static const ClipWord kClipWords[] = {']
for k,v in rows:
    out.append('    { "%s", "%s" },'%(k,esc(es(v))))
out.append('};')
open(sys.argv[1],'w').write('\n'.join(out)+'\n')
