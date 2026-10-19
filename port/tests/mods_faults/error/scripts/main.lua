-- An error in a frame: the mod stops, the others go on.
mod.on('frame', function(e) if e.frame % 300 == 0 then local t = nil; t.x = 1 end end)
