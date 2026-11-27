local exports = {}

local function getAddress(aob, binding)
  local address, reason = core.AOBScanUnique(aob)
  if address == nil then
    log(ERROR, string.format("GM Resource Modifier is off. The %s was %s.", binding, reason))
  end
  return address
end

--[[ Main Func ]]--

exports.enable = function(self, moduleConfig, globalConfig)

  --[[ get addresses ]]--

  local placeToReplaceLoadGmsAddr = getAddress(
    "e8 ? ? ? ff b9 ? ? ? 02 e8 ? ? ? ff 53 6a 03",
    "GM loading call"
  )
  if placeToReplaceLoadGmsAddr == nil then return end
  
  local actualLoadGmsAddress = getAddress(
    "53 55 8B 6C 24 0C 56 57 8B D9 33 C0 8D 64 24 00 83 C0 01 BF ? ? ? ? 8B F5 B9 05 00 00 00 33 D2 F3 A6 74 10 81 C5 E8 03 00 00 3D F0 00 00 00 7C DE 83 C0 01 A3 ? ? ? ? B8 01 00 00 00 33 ED 89 43 48 89 43 4C 89 6B 44 8D 9B 00 00 00 00 8B 44 24 14 83 C5 01 BF ? ? ? ? 8B F0 B9 05 00 00 00 33 D2 F3 A6 74 44 8B 4B 4C 8B 53 48 89 14 8D ? ? ? ? 50 8B 43 4C 50 8B CB E8 ? ? ? ? 8B 43 4C 81 44 24 14 E8 03 00 00 8B C8 69 C9 58 14 00 00 8B 94 19 28 05 00 00 01 53 48",
    "native GM loader"
  )
  if actualLoadGmsAddress == nil then return end

  -- Decode only inside the verified native loader context, before patching its
  -- existing call site. Another owner's replacement must not be overwritten.
  if placeToReplaceLoadGmsAddr + 5 + core.readInteger(placeToReplaceLoadGmsAddr + 1) ~= actualLoadGmsAddress then
    log(ERROR, "GM Resource Modifier is off. The GM loading call is already replaced or incompatible.")
    return
  end
  local gmFirstImageAddr = core.readInteger(actualLoadGmsAddress + 0x72)
  local gmCountAddr = core.readInteger(actualLoadGmsAddress + 0x36)
  
  local transformRGB555ToRGB565 = getAddress(
    "55 8b ec 83 ec 0c 8b 45 08",
    "RGB555 to RGB565 converter"
  )
  if transformRGB555ToRGB565 == nil then return end

  local gmImageHeaderAddr = getAddress(
    "c1 e0 04 81 c1 ? ? ? 00 50 51",
    "image header binding"
  )
  if gmImageHeaderAddr == nil then return end
  gmImageHeaderAddr = core.readInteger(gmImageHeaderAddr + 5)

  local gmSizesAddr = getAddress(
    "89 14 bd ? ? ? 00 eb 53",
    "GM sizes binding"
  )
  if gmSizesAddr == nil then return end
  gmSizesAddr = core.readInteger(gmSizesAddr + 3)

  local gmOffsetAddr = getAddress(
    "8b 2c bd ? ? ? 00 03 d7 3b fa",
    "GM offsets binding"
  )
  if gmOffsetAddr == nil then return end
  gmOffsetAddr = core.readInteger(gmOffsetAddr + 3)
  
  local pixelFormatAddr = getAddress(
    "81 3d ? ? ? 00 65 05 00 00 75 40",
    "pixel format binding"
  )
  if pixelFormatAddr == nil then return end
  pixelFormatAddr = core.readInteger(pixelFormatAddr + 2)


  --[[ load module ]]--
  
  local requireTable = require("gmResourceModifier.dll") -- loads the dll in memory and runs luaopen_gmResourceModifier
  
  -- no wrapping needed?
  self.LoadGm1Resource = function(self, ...) return requireTable.lua_LoadGm1Resource(...) end
  self.FreeGm1Resource = function(self, ...) return requireTable.lua_FreeGm1Resource(...) end
  self.SetGm = function(self, ...) return requireTable.lua_SetGm(...) end
  self.LoadResourceFromImage = function(self, ...) return requireTable.lua_LoadResourceFromImage(...) end
  self.LoadResourceFromImageData = function(self, ...) return requireTable.lua_LoadResourceFromImageData(...) end
  self.LoadGm1ResourceFromData = function(self, ...) return requireTable.lua_LoadGm1ResourceFromData(...) end
  self.EncodeImageAsTgx = function(self, ...) return requireTable.lua_EncodeImageAsTgx(...) end
  self.ReserveGm = function(self, ...) return requireTable.lua_ReserveGm(...) end
  self.GetReservedGm = function(self, ...) return requireTable.lua_GetReservedGm(...) end
  self.CreateOverlayResource = function(self, ...) return requireTable.lua_CreateOverlayResource(...) end
  

  --[[ modify code ]]--
  
  -- write the call to the own function
  core.writeCode(
    placeToReplaceLoadGmsAddr,
    {0xE8, requireTable.funcAddress_DetouredLoadGmFiles - placeToReplaceLoadGmsAddr - 5}  -- call to func
  )
  
  -- give actual load gms function
  core.writeCode(
    requireTable.address_ActualLoadGmsFunc,
    {actualLoadGmsAddress}
  )
  
  -- give actual RGB transform function
  core.writeCode(
    requireTable.address_TransformTgxFromRGB555ToRGB565,
    {transformRGB555ToRGB565}
  )
  
  -- give address of image header
  core.writeCode(
    requireTable.address_ShcImageHeaderStart,
    {gmImageHeaderAddr}
  )
  
  -- give address of image sizes
  core.writeCode(
    requireTable.address_ShcSizesStart,
    {gmSizesAddr}
  )
  
  -- give address of image offset
  core.writeCode(
    requireTable.address_ShcOffsetStart,
    {gmOffsetAddr}
  )

  core.writeCode(requireTable.address_ShcFirstImageStart, {gmFirstImageAddr})
  core.writeCode(requireTable.address_ShcGmCount, {gmCountAddr})
  
  -- give address to pixel format
  core.writeCode(
    requireTable.address_GamePixelFormatAddr,
    {pixelFormatAddr}
  )

  
  --[[ use config ]]--
  
  -- none at the moment


  --[[ test code ]]--
  
  --local resId = self.LoadGm1Resource("ucp/resources/tile_castle.gm1")
  --self.SetGm(0x34, -1, resId, -1)
  
  --local resId = self.LoadGm1Resource("gm/tile_castle.gm1")
  --self.SetGm(0x34, -1, resId, -1)
  
  --local resId2 = self.LoadGm1Resource("gm/anim_windmill.gm1")
  --self.SetGm(0x1a, -1, resId2, -1)

end

exports.disable = function(self, moduleConfig, globalConfig) error("not implemented") end

return exports