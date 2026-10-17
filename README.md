# PES21 FX (Pes_GraphicalUpgrade)

Mod grafico para eFootball PES 2021 (DX11) que se carga como modulo de Sider y reemplaza a 3DMigoto:
shaders mejorados (piel, pelo, kits con tejido y estampados, luz, sombras, rebote de luz, post), parametros
editables en vivo y un menu en pantalla (Insert).

- `src/` codigo de la DLL (MinHook + Dear ImGui + DDSTextureLoader de DirectXTK).
- `package/` lo que se instala: `PES21FX.lua`, `PES21FX/` (ini, reglas, shaders, texturas).
- GitHub Actions compila la DLL en cada push y deja el paquete listo como artefacto "PES21FX".

Instalacion: ver `package/LEEME.txt`.
