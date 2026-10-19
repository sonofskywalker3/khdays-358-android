-- A 100 MB table: refused past the mod's 64 MB, the mod goes on.
local ok, why = pcall(function() local t = {} for i = 1, 100 * 1024 * 1024 // 16 do t[i] = i end end)
print('memory', ok, why)
