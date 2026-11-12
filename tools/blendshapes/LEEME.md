# Gestos esculpidos por vertice (blendshapes) para la cara de Messi

Entrada: las mallas que vuelca la DLL con w354 = 1 (PES21FX\dump\caras\cara_<nv>v_<t>t_<huella>.obj + .txt). Las mallas NO van al repo.

    MALLAS=<carpeta con los .obj/.txt> python3 glb.py messi_gestos.glb    # .glb con shape keys para Blender
    MALLAS=<carpeta> python3 render2.py gestos.png                         # vista de adelante de cada gesto

- La piel de Messi viene en 3 pedazos (1379 v cabeza/cuello, 704 v mejillas/labio de arriba, 353 v menton/labio de abajo).
- shapes.py: pinceles por codigo (pomulo, surco nasolabial, entrecejo, nariz, mentoniano, comisuras...), en mm. Son CORRECTIVOS que se
  suman a lo que ya mueven los huesos del mod; el borde de los parpados y la linea entre los labios casi no se mueven (la boca no se toca).
- huesos_<gesto>: vista APROXIMADA de los huesos del mod con el gesto en 1 (sin el giro de la mandibula); la paleta -> hueso de
  mapping.py esta deducida a mano por posicion. Sirve solo para ver el total en Blender (prender las dos shape keys).
