// Prueba de TorsoBody_cuello.lua en una VM de Lua (fengari: npm install fengari). Uso: node test_cuello.js <modulo.lua> <face.fpk> <salida.fpk>
// fengari es Lua 5.3 con enteros de 32 bits: u32 se cambia a flotante solo para la prueba; se agregan atan2 / frexp / collectgarbage.
const fengari = require('fengari');
const {lua, lauxlib, lualib, to_luastring, to_jsstring} = fengari;
const fs = require('fs');
const [modPath, fpkPath, outPath] = process.argv.slice(2);
let src = fs.readFileSync(modPath, 'latin1');
// test only: accept the generic neck material as a face skin mesh
src = src.replace('if name == "fox_skin_mat" or name == "collar_ao" then', 'if name == "skin_head" then name = "fox_skin_mat" end\r\n        if name == "fox_skin_mat" or name == "collar_ao" then');
// FLOATU32 (fengari: enteros de 32 bits)
src = src.split('return a + b * 256 + c * 65536 + d * 16777216 end').join('return a + b * 256.0 + c * 65536.0 + d * 16777216.0 end');
const L = lauxlib.luaL_newstate(); lualib.luaL_openlibs(L);
const pre = `
math.frexp = math.frexp or function(x)
  if x == 0 then return 0, 0 end
  local e = math.floor(math.log(math.abs(x), 2)) + 1
  local m = x / 2^e
  if math.abs(m) >= 1 then m = m / 2; e = e + 1 elseif math.abs(m) < 0.5 then m = m * 2; e = e - 1 end
  return m, e
end
math.atan2 = math.atan2 or function(y, x) return math.atan(y, x) end
math.ldexp = math.ldexp or function(m, e) return m * 2.0^e end
math.pow = math.pow or function(a, b) return a^b end
collectgarbage = function() return 0 end
function log(s) print("LOG: " .. s) end
`;
if (lauxlib.luaL_dostring(L, to_luastring(pre)) !== lua.LUA_OK) { console.log(lua.lua_tojsstring(L,-1)); process.exit(1); }
const fpk = fs.readFileSync(fpkPath);
let out = null;
lua.lua_newtable(L);
lua.lua_pushjsfunction(L, (L) => { lua.lua_pushstring(L, new Uint8Array(fpk)); return 1; }); lua.lua_setfield(L, -2, to_luastring("read"));
lua.lua_pushjsfunction(L, (L) => { out = lua.lua_tostring(L, 2); return 0; }); lua.lua_setfield(L, -2, to_luastring("write"));
lua.lua_setglobal(L, to_luastring("memory"));
let st = lauxlib.luaL_loadbuffer(L, Buffer.from(src, 'latin1'), null, to_luastring("@TorsoBody_cuello.lua"));
if (st !== lua.LUA_OK) { console.log("load error", lua.lua_tojsstring(L,-1)); process.exit(1); }
st = lua.lua_pcall(L, 0, 1, 0);
if (st !== lua.LUA_OK) { console.log("run error", lua.lua_tojsstring(L,-1)); process.exit(1); }
lua.lua_setglobal(L, to_luastring("M"));
const call = `
local ctx = {}
M._test.data_ready(ctx, "common/character0/model/character/face/real/99999/#windx11/face.fpk", "ptr", ${fpk.length}, ${fpk.length}, 0)
`;
st = lauxlib.luaL_dostring(L, to_luastring(call));
if (st !== lua.LUA_OK) { console.log("call error", lua.lua_tojsstring(L,-1)); process.exit(1); }
if (out) { fs.writeFileSync(outPath, Buffer.from(out)); console.log("written", out.length); } else console.log("no output");
