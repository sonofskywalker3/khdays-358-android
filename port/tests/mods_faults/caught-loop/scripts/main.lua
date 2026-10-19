-- An endless loop that catches its own errors: paused all the same.
mod.on('frame', function() while true do pcall(function() while true do end end) end end)
