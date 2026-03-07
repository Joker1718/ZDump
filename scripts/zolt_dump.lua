-- zolt_dump.lua
-- Zolt-Dump — Godot Engine Dumper
-- CE Lua script for DLL injection and pipe communication
--
-- Usage: Execute this script in Cheat Engine after attaching to a Godot game

local PIPE_NAME = '\\\\.\\pipe\\ZoltDumpBfx'
local DLL_NAME = 'ZoltDumper.dll'

-- Check if DLL is already loaded
local function isDllLoaded()
    local mods = enumModules()
    if mods then
        for i = 1, #mods do
            if mods[i].Name:lower() == DLL_NAME:lower() then
                return true
            end
        end
    end
    return false
end

-- Find DLL path
local function findDllPath()
    local searchPaths = {
        getCheatEngineDir() .. '\\autorun\\' .. DLL_NAME,
        extractFilePath(TrainerOrigin or '') .. DLL_NAME,
        (os.getenv('LOCALAPPDATA') or '') .. '\\ZoltDump\\' .. DLL_NAME,
    }

    for _, path in ipairs(searchPaths) do
        local f = io.open(path, 'r')
        if f then
            f:close()
            return path
        end
    end
    return nil
end

-- Inject DLL
local function injectZoltDumper()
    if isDllLoaded() then
        print('[Zolt-Dump] DLL already loaded')
        return true
    end

    local path = findDllPath()
    if not path then
        print('[Zolt-Dump] ERROR: ' .. DLL_NAME .. ' not found')
        return false
    end

    print('[Zolt-Dump] Injecting: ' .. path)
    local result = injectDLL(path)
    if result then
        print('[Zolt-Dump] Injection successful')
    else
        print('[Zolt-Dump] Injection FAILED')
    end
    return result
end

-- Auto-start with C export
local function autoStart()
    if not isDllLoaded() then return false end

    -- Call Zolt_AutoStart export
    local mod = getAddress(DLL_NAME)
    if mod and mod ~= 0 then
        local exportAddr = getModuleExportAddress(DLL_NAME, 'Zolt_AutoStart')
        if exportAddr and exportAddr ~= 0 then
            executeCodeEx(0, nil, exportAddr)
            print('[Zolt-Dump] AutoStart called')
            return true
        end
    end
    return false
end

-- Main
print('=== Zolt-Dump — Godot Engine Dumper ===')
if injectZoltDumper() then
    sleep(1000) -- Wait for DLL init
    autoStart()
end
