-- A title entry: a list of three difficulties, then a page with the choice, then a result.
local names = {
  { en = "Easy", es = "Fácil" },
  { en = "Normal", es = "Normal" },
  { en = "Hard", es = "Difícil" },
}

local function result(i)
  screen.result{
    title = { en = "Result", es = "Resultado" },
    rows = {
      { { en = "Difficulty", es = "Dificultad" }, names[i] },
      { { en = "Waves", es = "Oleadas" }, 7 },
      { { en = "Time", es = "Tiempo" }, "3:20" },
    },
    on_close = function() print("result closed") end,
  }
end

title.entry{
  label = { en = "Test mode", es = "Modo de prueba" },
  on_select = function()
    print("entry chosen")
    screen.list{
      title = { en = "Difficulty", es = "Dificultad" },
      entries = names,
      on_choose = function(i)
        print("chose", i)
        screen.page{
          title = names[i],
          text = { en = "A page of the mod's own.\nIts second line.", es = "Una página del propio mod.\nSu segunda línea." },
          on_close = function() result(i) end,
        }
      end,
      on_cancel = function() print("cancelled") end,
    }
  end,
}
