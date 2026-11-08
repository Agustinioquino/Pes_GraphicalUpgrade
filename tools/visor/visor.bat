@echo off
rem PES21 FX - Visor de archivos del juego (solo lectura) + pagina del entorno visual.
rem Abre http://127.0.0.1:21020/ ; el panel en vivo del juego (DLL) escucha en http://127.0.0.1:21021/
cd /d "%~dp0"
where python >nul 2>nul || (echo Falta Python 3: https://www.python.org/downloads/ ^(marcar "Add to PATH"^) & pause & exit /b 1)
python -c "import numpy, PIL" 2>nul || (echo Instalando numpy y pillow... & python -m pip install --user numpy pillow)
start "" http://127.0.0.1:21020/
python visor.py --juego "D:\SteamLibrary\steamapps\common\eFootball PES 2021" --trabajo "B:\VisualPes21" %*
pause
