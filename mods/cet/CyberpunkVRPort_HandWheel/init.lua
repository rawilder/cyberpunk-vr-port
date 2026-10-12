-- CyberpunkVRPort — hand-anchored weapon wheel (CET side).
--
-- The plugin draws the wheel on the right hand and decides the selection from hand movement;
-- it talks to this module through two natives, the same shape as the settings overlay's bridge:
--   Game.VRHandWheelLabels("slot1|slot2|slot3|HOLSTER")  -- what to write on the sectors
--   Game.VRHandWheelCommand()                           -- 0 nothing, 1 opened, 2 closed, 100+i chosen
-- On open the game is slowed the way the native wheel slows it; on a choice the redscript helper
-- queues the equip through EquipmentSystem. Nothing here touches the native weapon wheel: the
-- plugin masks the Y hold before the game sees it.

local M = { labelsAt = 0.0, lastLabels = nil, slowed = false, dilation = "VRHandWheel" }

local function labels()
  local p = Game.GetPlayer()
  if not p then return nil end
  local out = {}
  for i = 0, 2 do
    local name = ""
    pcall(function() name = p:VRWheelSlotLabel(i) or "" end)
    if name == "" then name = "EMPTY" end
    out[#out + 1] = name
  end
  out[#out + 1] = "HOLSTER"
  return table.concat(out, "|")
end

local function setDilation(on)
  local ts = Game.GetTimeSystem()
  if not ts then return end
  if on then
    -- Signature differs between game versions; try the long form, then the short one.
    local ok = pcall(function() ts:SetTimeDilation(CName.new(M.dilation), 0.08, 0.0, CName.new("Linear"), CName.new("Linear"), nil) end)
    if not ok then ok = pcall(function() ts:SetTimeDilation(CName.new(M.dilation), 0.08) end) end
    M.slowed = ok
  elseif M.slowed then
    pcall(function() ts:UnsetTimeDilation(CName.new(M.dilation), CName.new("Linear")) end)
    pcall(function() ts:UnsetTimeDilation(CName.new(M.dilation)) end)
    M.slowed = false
  end
end

registerForEvent("onInit", function()
  print("[HandWheel] ready")
end)

registerForEvent("onUpdate", function(dt)
  if type(Game.VRHandWheelCommand) ~= "function" then return end
  M.labelsAt = M.labelsAt - dt
  if M.labelsAt <= 0 then
    M.labelsAt = 0.5
    local l = labels()
    if l and l ~= M.lastLabels then
      M.lastLabels = l
      pcall(function() Game.VRHandWheelLabels(l) end)
    end
  end
  local command = 0
  pcall(function() command = Game.VRHandWheelCommand() end)
  if command == 1 then
    setDilation(true)
  elseif command == 2 then
    setDilation(false)
  elseif command >= 100 then
    setDilation(false)
    local p = Game.GetPlayer()
    if p then pcall(function() p:VRWheelEquip(command - 100) end) end
  end
end)

registerForEvent("onShutdown", function() setDilation(false) end)
