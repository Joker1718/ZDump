-- zolt_dissect.lua
-- Zolt-Dump — CE Structure Dissect Builder
-- Creates CE structure definitions from Godot object layouts
--
-- Requires: ZoltDumper.dll loaded in game process

local DLL_NAME = 'ZoltDumper.dll'

-- Get export function address
local function getExport(name)
    local addr = getModuleExportAddress(DLL_NAME, name)
    if addr and addr ~= 0 then return addr end
    return nil
end

-- Call Zolt_WalkInstance and build structure
local function buildStructure(address)
    local walkAddr = getExport('Zolt_WalkInstance')
    if not walkAddr then
        print('[Zolt-Dissect] ERROR: Zolt_WalkInstance export not found')
        return
    end

    -- TODO: Call export, parse result, build CE structure
    -- This requires the mailbox/shared memory interface
    print('[Zolt-Dissect] Building structure for address: ' .. string.format('0x%X', address))
    print('[Zolt-Dissect] TODO: Implement via Qual (mailbox) interface')
end

-- Create structure dissect from selected address
local function dissectSelected()
    local addr = getAddress('$selectedAddress') or 0
    if addr == 0 then
        print('[Zolt-Dissect] No address selected')
        return
    end
    buildStructure(addr)
end

print('=== Zolt-Dump Structure Dissect Builder ===')
print('Use dissectSelected() to build a structure from the selected CE address')
