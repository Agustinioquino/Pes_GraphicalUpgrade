import re
_RAW="""
a='a',abs='absoluto',accel='aceleracion',action='accion',add='suma',adjust='ajuste',adjusted='ajustado',advance='avance',advantage='ley de ventaja',
advantageous='ventajoso',adverse='adverso (en contra)',after='despues de',aggressive='agresivo',ai='IA',air='aire',all='todo',allow='permitido',alone='solo',
always='siempre',angle='angulo',anime='animacion',approach='aproximacion',area='area',arrival='llegada',arrow='flecha',assign='asignacion',assist='asistencia',
at='en',attack='ataque',audienc='publico',auto='automatico',ave='promedio',average='promedio',avoid='evitar',awareness='conciencia',away='visitante',axis='eje',
b='B',back='atras/espalda',backward='hacia atras',ball='pelota',bank='inclinacion (bank)',base='base',basis='base',battle='duelo',before='antes',begin='inicio',behind='detras',
beside='al costado',blend='mezcla',blendthreshold='umbral de mezcla',block='bloqueo',board='cartel',body='cuerpo',border='umbral',bound='bote',box='area/caja',bp='portador (ballplayer)',
break='ruptura',breaker='rompedor',breakthrough='penetracion',bronze='bronce',bruise='contusion',build='construccion',buoyancy='sustentacion',burst='arranque/sprint explosivo',
busy='ocupado',button='boton',by='por',c='C',calc='calculo',camera='camara',cancel='cancelacion',catch='atajada',catchable='atajable',category='categoria',categorys='categorias',
caution='precaucion',cb='central (CB)',center='centro',centering='centro (cruce)',challenge='desafio',chance='ocasion',change='cambio',chapeu='sombrerito (chapeu)',charge='carga',
chase='persecucion',check='chequeo',checker='verificador',circle='circulo',classical='clasico',clear='despeje',click='clic',close='cierre/cerca',code='codigo',coef='coeficiente',
collapsing='derrumbe/caida',collision='colision',com='CPU',combs='combinaciones',command='comando',compact='compacto',concept='concepto',condition='estado',cone='cono',
config='configuracion',connect='conexion',connection='conexion',contact='contacto',continue='continuar',continuous='continuo',control='control',corner='corner',cornerkick='tiro de esquina',
correct='correccion',count='cantidad',counter='contraataque',course='trayectoria',cover='cobertura',cpustyle='estilo CPU',create='crear',cross='centro/cruce',csv='CSV',cursor='cursor',
curve='curva',custom='personalizado',customize='personalizar',cut='corte',cuve='curva',damage='dano',danger='peligro',dash='pique/sprint',data='datos',debug='depuracion',
dec='decremento',decelerate='desacelerar',decide='decision',decline='descenso',default='por defecto',defence='defensa',defense='defensa',defensive='defensivo',deflect='desvio',
delay='demora/retardo',delete='borrar',delta='delta',demo='demo',detail='detalle',df='defensor (DF)',diagonal='diagonal',diff='diferencia',dir='direccion',direct='directo',
direction='direccion',disable='desactivar',disp='visualizacion',dist='distancia',distance='distancia',div='division',dive='estirada',dodge='esquive',down='baja',drag='arrastre aerodinamico',
dribble='regate',drop='caida',dummy='relleno',dy='dY (altura)',eary='temprano',effect='efecto',elapsed='transcurrido',enable='activado',enabled='activado',end='fin',
eneble='activado',enemy='rival',enter='entrada',entrance='entrada',entry='ingreso',exec='ejecutar',eye='mirada',eyes='mirada',fail='fallo',failure='fracaso',fake='finta',
fall='caida',falldown='caida al suelo',fan='abanico',far='lejos',fast='rapido',feint='finta',filter='filtro',first='primero',fish='pez',fix='fijo',fixation='fijacion',fixed='fijo',
fk='tiro libre',flag='bandera',float='float',fly='aereo',follow='seguimiento',foot='pie',for='para',force='forzado',forced='forzado',fore='delantero',forecast='prediccion',
formation='formacion',foul='falta',fov='campo de vision (FOV)',fracture='fractura',frame='cuadros',free='libre',freekick='tiro libre',friction='friccion',from='desde',front='frente',
future='futuro',fw='delantero (FW)',gage='barra de potencia',gap='hueco',get='recepcion',gk='arquero',gkl='saque largo arquero',gks='saque corto arquero',goal='arco/gol',gold='oro',
good='bueno (nivel alto)',goto='ir a',gounnderprob='prob. rastrero',grade='grado',grnd='rastrero',ground='suelo',grounder='rastrero',group='grupo',h='alto',half='mitad',hand='mano',
hard='fuerte',heel='taco',height='altura',high='alto',highball='pelota alta',highest='maximo',hit='impacto',hold='retener',home='local',ideal='ideal',idle='parado',ik='IK',
in='dentro',infinity='infinito',inflammation='inflamacion',info='info',injection='inyeccion',inner='interior',inplay='en juego',input='entrada (palanca)',int='entero',
intention='intencion',intercept='intercepcion',interpolate='interpolar',interrupt='interrumpir',interval='intervalo',jog='trote',jostle='forcejeo',json='JSON',judge='juicio',
jump='salto',keep='mantener',keeper='arquero',key='tecla',kick='patada',kickanime='animacion de patada',kicker='pateador',kickoff='saque inicial',kind='tipo',king='goleador',
kp='punto de pateo',kph='km/h',l='izq.',laceration='laceracion',large='grande',last='ultima',late='tarde',lay='descarga',left='izq.',len='longitud',length='longitud',lerp='interpolar',
level='nivel',levelup='subir nivel',ligament='ligamento',lim='limite',limit='limite',limiti='limite',line='linea',list='lista',lmt='limite',lob='globo',local='local',log='log',
long='largo',longitudinal='longitudinal',longpass='pase largo',look='mirada',loop='bombeo/vaselina',loose='suelta',lose='perder',loss='perdida',lost='perdido',low='bajo',
lower='inferior',lowest='minimo',lush='carrera (rush)',lv='nivel',magnus='efecto Magnus',main='principal',mani='manivela',manual='manual',many='muchos',margin='margen',mark='marca',
marked='marcado',match='partido',mate='companero',max='max',median='mediana',medical='medico',mergetime='tiempo de fusion',metabolism='recuperacion (metabolismo)',mf='mediocampista (MF)',
micro='micro',mid='medio',middle='medio',min='min',minor='menor',minus='menos',mirror='espejo',mle='estimacion MLE',mode='modo',motion='movimiento',mouse='raton',move='movimiento',
movetime='tiempo de movimiento',moving='en movimiento',multiply='multiplicador',muscle='muscular',must='obligatorio',my='propio',name='nombre',natual='natural',natunal='natural',
natural='natural',near='cerca',need='necesario',neutral='neutral (palanca suelta)',new='nuevo',next='siguiente',no='sin',non='sin',normal='normal',now='actual',num='cantidad',
number='numero',numerical='numerico',object='objeto',of='de',off='apagado',offence='ataque',offencive='ofensivo',offensive='ofensivo',offer='oferta/pedido',offset='desplazamiento',
offside='fuera de juego',on='en',one='uno',only='solo',onoff='encendido',open='abierto',opp='rival',or='o',other='otro',out='fuera',output='salida',over='sobre/mas de',own='propio',
p='punto',pad='mando',pan='paneo',panel='panel',pant='puntapie (punt)',para='parametro (stat)',param='parametro',parameter='parametro',pass='pase',passget='recepcion de pase',
pattern='patron',penalty='penal',penetrate='penetrar',percent='porcentaje',personal='personal',pes='PES',physical='fisico',pillar='pilar',pitch='cancha',pixcel='pixel',pk='penal',
play='jugada',playback='reproduccion',player='jugador',players='jugadores',playstyle='estilo de juego',plus='mas',point='punto',pos='posicion',position='posicion',possible='posible',
power='potencia',pre='pre',prediction='prediccion',press='presion',pressure='presion',prior='prioridad',priority='prioridad',prohibition='prohibido',project='proyecto',protect='proteger',
pull='tirar/desmarque',punch='punos',punchable='despejable con punos',push='empuje',pylon='cono',quick='rapido',r='der.',radius='radio',ragdoll='ragdoll',range='rango',rate='tasa/factor',
rating='calificacion',ratio='proporcion',reach='alcance',reaction='reaccion',rear='trasero',receive='recepcion',recieve='recepcion',rect='rectangulo',reg='pierna',regist='resistencia',
relation='relacion',relay='transicion',release='liberar',remain='restante',repeat='repetir',request='pedido',restart='reanudacion',result='resultado',retreat='repliegue',retry='reintento',
return='regreso',rev='inverso',reverse='inverso',right='der.',rigth='der.',rise='subida',roll='rodadura',rot='rotacion',rotation='rotacion',round='ronda',route='ruta',run='carrera',
runner='corredor',running='corriendo',safe='seguro',safty='seguridad',sand='sandwich (dos marcas)',save='atajada',sb='lateral (SB)',scene='escena',scoop='recoger',score='puntaje',
seamless='sin corte',search='busqueda',seat='tribuna',sec='seg',second='segunda',sector='sector',serious='grave',set='set',setid='id de set',setplay='balon parado',setting='ajuste',
setup='preparacion',shift='desplazamiento',sho='remate',shoot='tiro',short='corto',shot='toma',should='debe',shoulder='hombro',side='lado/banda',sidetrap='trampa',silver='plata',
sin='seno',situation='situacion',size='tamano',skill='habilidad',slide='deslizamiento',sliding='barrida',slow='lento',smooth='suavizado',snap='manotazo',snatch='robo',snatched='robado',
space='espacio',specialist='especialista',speed='velocidad',spin='efecto (spin)',stagger='trastabillar',stamina='resistencia (stamina)',start='inicio',step='paso',stick='palanca',
stop='parada',straight='recto',stratagy='estrategia',strategy='estrategia',strong='fuerte',stronger='mas fuerte',style='estilo',sub='sub/diferencia',subtle='sutil',success='exito',
super='super',support='apoyo',suspend='suspender',swipe='deslizar (tactil)',symptom='sintoma',table='tabla',tackle='entrada (tackle)',taking='tomando',tan='tangente',tap='toque',
target='objetivo',team='equipo',tear='desgarro',technical='tecnica',test='prueba',the='el',thickness='grosor',thigh='muslo',think='decision IA',threshold='umbral',through='filtrado',
throw='saque',throwin='saque de banda',time='tiempo',timeout='tiempo agotado',timer='temporizador',timing='momento',tired='cansancio',tmp='temporal',to='a',too='demasiado',top='alto/top',
touch='toque',trans='transicion',transform='transformacion',transition='transicion',trap='control (recepcion)',turn='giro',tutorial='tutorial',two='dos',type='tipo',uefa='UEFA',
under='bajo',unit='unidad',until='hasta',up='sube',update='actualizar',upper='superior',use='usar',user='usuario',v='V',valid='valido',value='valor',variation='variacion',vec='vector',
velocity='velocidad',version='version',visible='visible',vital='vital',vs='vs',w='ancho',wait='espera',walk='caminar',wall='barrera/pared',weak='debil',wedge='cuna',week='debil',
wide='amplio',width='ancho',work='trabajo',x='X',xposi='posicion X',xz='XZ (plano)',y='Y (altura)',z='Z',zero='cero',zone='zona',zposi='posicion Z',zz='zz'"""
T=dict(re.findall(r"(\w+)='([^']*)'",_RAW))
def gloss(path):
    keys=[x for x in path if isinstance(x,str)]
    out=[]
    for k in keys:
        toks=re.findall(r'[A-Z]?[a-z]+|[A-Z]+(?![a-z])|\d+',k)
        w=[]
        for t in toks:
            tl=t.lower()
            if tl=='is' and not w: w.append('activa?'); continue
            w.append(T.get(tl,tl))
        out.append(' '.join(w))
    s=' / '.join(out)
    return s
