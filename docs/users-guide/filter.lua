-- Pandoc filter for the user's guide (Markdown -> Typst).
--
--   ```{.screen session=NAME}     the captured screen build/screens/NAME.txt (see screens.py)
--   ```{.listing file=card/X.BAS} a program listing, read from the file the sessions run
--   ::: curious / try / careful   the three kinds of box (guide.typ defines them)
--   [ESC]{.key}                   a key cap
--   ![caption](shots/x.png)       pictures live under build/, at /build/... for Typst

local function read(path)
  local f = io.open(path)
  if not f then error("filter.lua: cannot read " .. path .. " (run screens.py)") end
  local s = f:read("a")
  f:close()
  return s
end

function CodeBlock(el)
  -- ```{.listing file=card/NAME.BAS}: the file itself, so the listing shown is the one that ran
  if el.classes:includes("listing") and el.attributes.file then
    local text = read(el.attributes.file):gsub("\r", "")
    return pandoc.CodeBlock(text:gsub("\n+$", ""), pandoc.Attr("", { "listing" }))
  end
  if el.classes:includes("screen") and el.attributes.session then
    local text = read("build/screens/" .. el.attributes.session .. ".txt")
    return pandoc.CodeBlock(text:gsub("\n+$", ""), pandoc.Attr("", { "screen" }))
  end
end

local boxes = { curious = true, try = true, careful = true }

function Div(el)
  for _, c in ipairs(el.classes) do
    if boxes[c] then
      local out = { pandoc.RawBlock("typst", "#" .. c .. "[") }
      for _, b in ipairs(el.content) do out[#out + 1] = b end
      out[#out + 1] = pandoc.RawBlock("typst", "]")
      return out
    end
  end
end

function Span(el)
  if el.classes:includes("key") then
    local out = { pandoc.RawInline("typst", "#key[") }
    for _, i in ipairs(el.content) do out[#out + 1] = i end
    out[#out + 1] = pandoc.RawInline("typst", "]")
    return out
  end
end

-- Column headings: #th[...] (a table without headings has an empty head and is left alone).
function Table(el)
  for _, row in ipairs(el.head.rows) do
    for _, cell in ipairs(row.cells) do
      for _, blk in ipairs(cell.contents) do
        if blk.content then
          table.insert(blk.content, 1, pandoc.RawInline("typst", "#th["))
          table.insert(blk.content, pandoc.RawInline("typst", "]"))
        end
      end
    end
  end
  return el
end

function Image(el)
  el.src = "/build/" .. el.src
  return el
end
