import sys; sys.path.insert(0,'/home/user/Pes_GraphicalUpgrade/tools')
import vista_juego as V
P=V.leer_pista(sys.argv[1]); out=open(sys.argv[2],'w')
quien={'A':'A (Messi, 170 cm, rojo)','B':'B (Ronaldo, 185 cm, azul)'}
out.write('Caras y manos que se reproducen en zfx_saludo v5 (30 cuadros por segundo). Peso = cuanto se ve el gesto (0 a 1).\n')
out.write('Encima de todo va el parpadeo add_eyeblink_single: A cada 3.65 s, B cada 3.35 s.\n\n')
for L in 'AB':
    out.write('Jugador %s\n' % quien[L])
    out.write('  segundo  cuadro  cara (peso)                         mano izquierda    mano derecha\n')
    for f,c,w,ml,mr in P[L]:
        out.write('  %6.1f  %6d  %-34s  %-16s  %s\n' % (f/30.0, f, '%s (%.2f)'%(c,w), ml, mr))
    out.write('\n')
