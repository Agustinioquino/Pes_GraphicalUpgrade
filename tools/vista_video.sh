# uso: hacer8.sh <carpeta gani> <vN> "<titulo>"   (una vista grande + tres chicas, para ver en el celular)
set -e
cd /tmp/claude-0/-home-user/1b69b908-deea-51b0-a152-c4a375b7e628/scratchpad
O=/mnt/project-files/motor/animaciones/vista_juego/zfx_saludo_juego_$2
rm -rf vj/cuadros vj/rot
export VISTAS='[{"n":"de frente","yaw":35,"pitch":-9,"dist":3.7,"fov":34,"k":1.3,"k0":1.0,"r":[0,0,0.664,1]},{"n":"de cerca","yaw":35,"pitch":-14,"dist":1.7,"fov":30,"ty":1.36,"k":0.9,"k0":1.0,"r":[0.666,0.036,0.334,0.30]},{"n":"de costado","yaw":90,"pitch":-4,"dist":3.0,"fov":34,"k":0.95,"k0":0.9,"r":[0.666,0.338,0.334,0.30]},{"n":"de atras","yaw":215,"pitch":-9,"dist":3.0,"fov":34,"k":0.9,"k0":1.6,"r":[0.666,0.640,0.334,0.296]}]'
TAM=1280x720 node run_vj.js $PWD/vj ${4:-todos} 1 2>&1 | grep -v '^page' | tail -1
INC=$1/motorpista.inc TITULO="$3" python3 rot8.py
[ -n "$4" ] && exit 0
ffmpeg -y -loglevel error -framerate 30 -pattern_type glob -i 'vj/rot/*.png' -c:v libx264 -pix_fmt yuv420p -crf 21 $O.mp4
ffprobe -v error -show_entries format=duration -of csv=p=0 $O.mp4
sed -i "s/zfx_saludo v[0-9]*/zfx_saludo $2/" caras_txt.py
python3 caras_txt.py $1/motorpista.inc ${O}_caras.txt
echo LISTO
