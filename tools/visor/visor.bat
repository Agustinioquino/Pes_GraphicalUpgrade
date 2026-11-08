@echo off
rem PES21 FX Visor - aplicacion de escritorio (ventana propia). El panel en vivo del juego (DLL) escucha en 127.0.0.1:21021.
cd /d "%~dp0"
where python >nul 2>nul || (echo Falta Python 3: https://www.python.org/downloads/ ^(marcar "Add to PATH"^) & pause & exit /b 1)
python -c "import numpy, PIL, webview" 2>nul || (echo Instalando numpy, pillow y pywebview... & python -m pip install --user numpy pillow pywebview)
python visor.py --juego "D:\SteamLibrary\steamapps\common\eFootball PES 2021" --trabajo "B:\VisualPes21" %*
if errorlevel 1 pause
