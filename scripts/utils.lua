-- utils.lua
-- Zolt-Dump — CE Lua Utility Functions

local Utils = {}

-- Convert hex string to number
function Utils.hexToNum(hex)
    if type(hex) == 'string' then
        hex = hex:gsub('^0x', ''):gsub('^0X', '')
        return tonumber(hex, 16)
    end
    return hex
end

-- Format number as hex string
function Utils.numToHex(num, width)
    width = width or 16
    return string.format('%0' .. width .. 'X', num or 0)
end

-- Read null-terminated UTF-8 string from memory
function Utils.readString(address, maxLen)
    maxLen = maxLen or 256
    local bytes = readBytes(address, maxLen, true)
    if not bytes then return '' end

    local result = {}
    for i = 1, #bytes do
        if bytes[i] == 0 then break end
        result[#result + 1] = string.char(bytes[i])
    end
    return table.concat(result)
end

-- Check if module is loaded
function Utils.isModuleLoaded(name)
    local mods = enumModules()
    if mods then
        for i = 1, #mods do
            if mods[i].Name:lower() == name:lower() then
                return true, mods[i].Address
            end
        end
    end
    return false, 0
end

-- Safe memory read with error handling
function Utils.safeRead(address, size)
    local ok, result = pcall(function()
        return readBytes(address, size, true)
    end)
    if ok then return result end
    return nil
end

-- Read pointer (8 bytes, little-endian)
function Utils.readPtr(address)
    return readQword(address)
end

-- Read int32
function Utils.readInt32(address)
    return readInteger(address)
end

-- Read float
function Utils.readFloat(address)
    return readFloat(address)
end

-- Print table contents (debug)
function Utils.printTable(t, indent)
    indent = indent or ''
    if type(t) ~= 'table' then
        print(indent .. tostring(t))
        return
    end
    for k, v in pairs(t) do
        if type(v) == 'table' then
            print(indent .. tostring(k) .. ':')
            Utils.printTable(v, indent .. '  ')
        else
            print(indent .. tostring(k) .. ' = ' .. tostring(v))
        end
    end
end

return Utils
