-- Small helpers shared by the bootstrap modules.
local M = {}

function M.edit_distance(a, b)
  local prev, cur = {}, {}
  for j = 0, #b do prev[j] = j end
  for i = 1, #a do
    cur[0] = i
    local ca = a:sub(i, i)
    for j = 1, #b do
      local cost = (ca == b:sub(j, j)) and 0 or 1
      cur[j] = math.min(prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost)
    end
    prev, cur = cur, prev
  end
  return prev[#b]
end

-- Closest candidate within a length-scaled budget, or nil. `candidates` is a list.
function M.closest(word, candidates)
  word = tostring(word)
  local budget = math.max(2, math.floor(#word / 3))
  local best, best_d
  for _, c in ipairs(candidates) do
    local d = M.edit_distance(word, c)
    if d <= budget and (best_d == nil or d < best_d or (d == best_d and c < best)) then
      best, best_d = c, d
    end
  end
  return best
end

function M.did_you_mean(word, candidates)
  local c = M.closest(word, candidates)
  return c and ("did you mean '" .. c .. "'?") or nil
end

local function mixed_less(a, b)
  local ta, tb = type(a), type(b)
  if ta ~= tb then return ta < tb end
  if ta == "string" or ta == "number" then return a < b end
  return tostring(a) < tostring(b)
end

function M.sorted_keys_of_set(set)
  local out = {}
  for k in pairs(set) do out[#out + 1] = k end  -- order-insensitive: sorted below
  table.sort(out, mixed_less)
  return out
end

function M.is_list(t)
  if type(t) ~= "table" then return false end
  local n = 0
  for _ in pairs(t) do n = n + 1 end  -- order-insensitive: count
  for i = 1, n do
    if t[i] == nil then return false end
  end
  return true
end

-- Lexical normalisation of an absolute path; nil if '..' escapes the root.
function M.normalize_path(path)
  local parts = {}
  for part in path:gmatch("[^/]+") do
    if part == ".." then
      if #parts == 0 then return nil end
      parts[#parts] = nil
    elseif part ~= "." then
      parts[#parts + 1] = part
    end
  end
  return "/" .. table.concat(parts, "/")
end

function M.dirname(path)
  local d = path:match("^(.*)/[^/]*$")
  if d == nil then return "." end
  if d == "" then return "/" end
  return d
end

function M.starts_with_path(path, prefix)
  if prefix == "/" then return path:sub(1, 1) == "/" end
  return path == prefix or path:sub(1, #prefix + 1) == prefix .. "/"
end

function M.shallow_copy(t)
  local out = {}
  for k, v in pairs(t) do out[k] = v end  -- order-insensitive: copy
  return out
end

function M.deep_copy(v, seen)
  if type(v) ~= "table" then return v end
  seen = seen or {}
  if seen[v] then return seen[v] end
  local out = {}
  seen[v] = out
  for k, x in pairs(v) do out[M.deep_copy(k, seen)] = M.deep_copy(x, seen) end  -- order-insensitive: copy
  return out
end

return M
