-- zolt_managed_records.lua
-- Zolt-Dump — .NET Managed Object Dynamic Memory Records
--
-- Generates CE memory records for .NET (C#) Godot game objects.
-- Uses CMD_FIND_MANAGED_INSTANCES (Qual mailbox cmd=6) for bulk DLL-side scan.
-- The DLL iterates ObjectDB, resolves .NET chains, and filters in a single pass
-- (~500ms for 50k objects), then CE Lua reads results from g_zoltManagedResults.
--
-- Usage:
--   1. Customize CONFIG section for your game
--   2. Embed in a CT Auto Assembler entry (see bottom of file)
--   3. [ENABLE] scans ObjectDB and creates memory records
--   4. [DISABLE] removes all generated records
--
-- Requires: ZoltDumper.dll injected and initialized (Zolt_AutoStart complete)

-- ============================================================
-- Configuration — Customize for your target game
-- ============================================================
-- Example: Slay the Spire 2 — Creature HP tracking

local CONFIG = {
    -- Godot class name to search in ObjectDB (the C++ base class).
    -- Use "" to search ALL objects with managed objects (slower).
    GODOT_CLASS = "Control",

    -- Fully qualified .NET type name for reflection-based offset resolution.
    -- When set, field names (fieldName) are resolved to offsets at runtime via
    -- the Metode CLR bridge. This makes scripts survive game updates that change
    -- field layouts but keep the same field names.
    -- Set to nil or "" to use hardcoded offsets only.
    DOTNET_TYPE = "MegaCrit.Sts2.Core.Entities.Creatures.Creature",

    -- .NET managed object field definitions
    -- name:      Display name in CE address list
    -- fieldName: .NET field name for reflection-based offset resolution (optional)
    --            Supports C# names including backing fields: "<Prop>k__BackingField"
    --            When DOTNET_TYPE is set, fieldName is resolved to offset automatically.
    -- offset:    Byte offset within the .NET managed object (fallback if reflection fails)
    -- ceType:    CE type enum (vtByte=0, vtWord=1, vtDword=2, vtQword=3, vtSingle=4, vtDouble=5)
    FIELDS = {
        { name = "HP",    fieldName = "_currentHp",              offset = 0x84, ceType = 2 },
        { name = "MaxHP", fieldName = "_maxHp",                  offset = 0x88, ceType = 2 },
        { name = "Side",  fieldName = "<Side>k__BackingField",   offset = 0x8C, ceType = 2 },
    },

    -- Filter: only include objects where a managed field matches a condition.
    -- The filter is applied DLL-side (fast, no per-object roundtrip).
    -- Set to nil to include ALL managed objects of GODOT_CLASS.
    --
    -- fieldName: .NET field name for reflection resolution (optional, preferred)
    -- offset: byte offset of the filter field within managed object (fallback)
    -- op:     comparison operator ("==", "!=", ">", "<", ">=", "<=")
    -- value:  comparison value (integer)
    -- size:   read size in bytes (1, 2, 4, or 8). Default: 4
    FILTER = {
        fieldName = "<Side>k__BackingField",
        offset = 0x8C,  -- fallback if reflection fails
        op     = "!=",
        value  = 1,
        size   = 4,
    },

    -- Which fields to create as memory records (by name from FIELDS table).
    -- nil = create records for ALL fields.
    DISPLAY_FIELDS = { "HP", "MaxHP" },

    -- Description prefix for generated records
    PREFIX = "Creature",

    -- Group description for the parent node
    GROUP_DESC = "[.NET Managed] Creatures",

    -- Mailbox poll timeout in milliseconds (DLL scan + filter)
    POLL_TIMEOUT_MS = 15000,

    -- Poll interval in milliseconds
    POLL_INTERVAL_MS = 50,
}

-- ============================================================
-- Mailbox constants (must match Qual.h)
-- ============================================================

local MB = {
    -- Offsets within g_zoltMailbox
    CMD            = 0x000,
    STATUS         = 0x004,
    RESULT         = 0x008,
    MEMBER_INDEX   = 0x00C,
    OBJECT_ADDR    = 0x010,
    MEMBER_ADDR    = 0x018,
    MEMBER_TYPE    = 0x020,
    CLASS_NAME     = 0x028,
    MEMBER_NAME    = 0x128,
    NODE_NAME      = 0x228,
    ERROR_MSG      = 0x328,
    TARGET_OP      = 0x428,
    TARGET_VALUE   = 0x430,
    FILTER_OP      = 0x438,
    FILTER_VALUE   = 0x440,
    MATCH_STR      = 0x448,

    -- Commands
    CMD_IDLE                   = 0,
    CMD_FIND_MANAGED_INSTANCES = 6,
    CMD_RESOLVE_FIELD_OFFSET   = 7,

    -- Status
    STATUS_IDLE = 0,
    STATUS_DONE = 1,
    STATUS_INIT = 0xFF,

    -- FilterOp enum
    FILTER_NONE = 0,
    FILTER_GT   = 1,
    FILTER_LT   = 2,
    FILTER_NE   = 3,
    FILTER_EQ   = 4,
    FILTER_GE   = 5,
    FILTER_LE   = 6,
}

-- Results buffer offsets (g_zoltManagedResults)
local RES = {
    COUNT    = 0x000,  -- int32 count
    CAPACITY = 0x004,  -- int32 capacity (256)
    ENTRIES  = 0x008,  -- start of entries array
    ENTRY_SIZE = 16,   -- each entry: objectAddr(8) + managedAddr(8)
}

-- ============================================================
-- Internal state
-- ============================================================

local TAG = "[Zolt-ManagedRec]"
local generatedRecords = {}  -- Track created memory records for cleanup
local groupRecord = nil      -- Parent group record

-- ============================================================
-- Helpers
-- ============================================================

local function log(fmt, ...)
    local msg = string.format("%s " .. fmt, TAG, ...)
    print(msg)
    if zolt_log then
        zolt_log(fmt, ...)
    end
end

local function logErr(fmt, ...)
    local msg = string.format("%s ERROR: " .. fmt, TAG, ...)
    print(msg)
    if zolt_logError then
        zolt_logError(fmt, ...)
    end
end

--- Convert Lua-style operator string to Qual FilterOp enum.
local function opToFilterOp(op)
    if op == ">"  then return MB.FILTER_GT end
    if op == "<"  then return MB.FILTER_LT end
    if op == "!=" then return MB.FILTER_NE end
    if op == "==" then return MB.FILTER_EQ end
    if op == ">=" then return MB.FILTER_GE end
    if op == "<=" then return MB.FILTER_LE end
    return MB.FILTER_NONE
end

--- Check if a field name should be displayed.
local function shouldDisplay(fieldName)
    if not CONFIG.DISPLAY_FIELDS then return true end
    for _, name in ipairs(CONFIG.DISPLAY_FIELDS) do
        if name == fieldName then return true end
    end
    return false
end

--- Write a null-terminated string to memory at addr.
local function writeStringToAddr(addr, str, maxLen)
    maxLen = maxLen or 256
    local len = math.min(#str, maxLen - 1)
    for i = 1, len do
        writeBytes(addr + i - 1, string.byte(str, i))
    end
    writeBytes(addr + len, 0)  -- null terminator
end

--- Read a null-terminated string from memory.
local function readStringFromAddr(addr, maxLen)
    maxLen = maxLen or 256
    local bytes = readBytes(addr, maxLen, true)
    if not bytes then return "" end
    local result = {}
    for i = 1, #bytes do
        if bytes[i] == 0 then break end
        result[#result + 1] = string.char(bytes[i])
    end
    return table.concat(result)
end

-- ============================================================
-- Reflection: Resolve .NET field names → offsets via DLL
-- ============================================================

--- Resolve a single .NET field name to its offset via CMD_RESOLVE_FIELD_OFFSET.
--- Returns offset (int) and size (int), or nil on failure.
local function resolveOneField(mb, typeName, fieldName)
    -- Wait for mailbox idle
    local waitStart = os.clock()
    while readInteger(mb + MB.STATUS) ~= MB.STATUS_IDLE do
        sleep(10)
        if (os.clock() - waitStart) * 1000 > 5000 then
            logErr("Timeout waiting for mailbox idle before resolve")
            return nil, nil
        end
    end

    -- Write typeName to className slot
    writeStringToAddr(mb + MB.CLASS_NAME, typeName, 256)

    -- Write fieldName to memberName slot
    -- .NET backing field names like "<Side>k__BackingField" contain angle brackets.
    -- These are safe in CE Lua strings and writeBytes — no special escaping needed.
    writeStringToAddr(mb + MB.MEMBER_NAME, fieldName, 256)

    -- Trigger command
    writeInteger(mb + MB.CMD, MB.CMD_RESOLVE_FIELD_OFFSET)

    -- Poll for completion
    local pollStart = os.clock()
    while true do
        local st = readInteger(mb + MB.STATUS)
        if st == MB.STATUS_DONE then break end

        sleep(CONFIG.POLL_INTERVAL_MS)
        if (os.clock() - pollStart) * 1000 > 5000 then
            logErr("Timeout resolving field '%s'", fieldName)
            return nil, nil
        end
    end

    local result = readInteger(mb + MB.RESULT)
    if result < 0 then
        local errMsg = readStringFromAddr(mb + MB.ERROR_MSG, 256)
        logErr("Failed to resolve '%s.%s': %s (code=%d)",
            typeName, fieldName, errMsg, result)
        return nil, nil
    end

    local fieldSize = readInteger(mb + MB.MEMBER_INDEX)
    return result, fieldSize
end

--- Resolve all field offsets in CONFIG.FIELDS and CONFIG.FILTER via reflection.
--- Falls back to hardcoded offsets if resolution fails for a given field.
local function resolveFieldOffsets()
    if not CONFIG.DOTNET_TYPE or CONFIG.DOTNET_TYPE == "" then
        log("No DOTNET_TYPE configured, using hardcoded offsets")
        return
    end

    local mb = getAddress("g_zoltMailbox")
    if not mb or mb == 0 then
        logErr("g_zoltMailbox not found, cannot resolve field offsets")
        return
    end

    log("Resolving .NET field offsets for type '%s'...", CONFIG.DOTNET_TYPE)

    -- Resolve FIELDS entries
    local resolved = 0
    for _, field in ipairs(CONFIG.FIELDS) do
        if field.fieldName and field.fieldName ~= "" then
            local offset, size = resolveOneField(mb, CONFIG.DOTNET_TYPE, field.fieldName)
            if offset then
                log("  Resolved '%s' → offset=0x%X (was 0x%X), size=%d",
                    field.fieldName, offset, field.offset, size or 0)
                field.offset = offset
                resolved = resolved + 1
            else
                log("  WARN: '%s' not resolved, keeping hardcoded offset 0x%X",
                    field.fieldName, field.offset)
            end
        end
    end

    -- Resolve FILTER field
    if CONFIG.FILTER and CONFIG.FILTER.fieldName and CONFIG.FILTER.fieldName ~= "" then
        local offset, size = resolveOneField(
            mb, CONFIG.DOTNET_TYPE, CONFIG.FILTER.fieldName)
        if offset then
            log("  Resolved filter '%s' → offset=0x%X (was 0x%X)",
                CONFIG.FILTER.fieldName, offset, CONFIG.FILTER.offset)
            CONFIG.FILTER.offset = offset
            if size then CONFIG.FILTER.size = size end
            resolved = resolved + 1
        else
            log("  WARN: filter '%s' not resolved, keeping hardcoded offset 0x%X",
                CONFIG.FILTER.fieldName, CONFIG.FILTER.offset)
        end
    end

    log("Reflection resolved %d field(s)", resolved)
end

-- ============================================================
-- Core: Bulk Scan via Mailbox
-- ============================================================

local function scanAndGenerate(parentRecord)
    -- Resolve .NET field names to offsets (updates CONFIG.FIELDS/FILTER in-place)
    resolveFieldOffsets()

    -- Resolve mailbox and results buffer addresses
    local mb = getAddress("g_zoltMailbox")
    if not mb or mb == 0 then
        logErr("g_zoltMailbox symbol not found (DLL not injected?)")
        return 0
    end

    local resBuf = getAddress("g_zoltManagedResults")
    if not resBuf or resBuf == 0 then
        logErr("g_zoltManagedResults symbol not found (DLL too old?)")
        return 0
    end

    -- Wait for DLL to be ready
    local status = readInteger(mb + MB.STATUS)
    if status == MB.STATUS_INIT then
        log("Waiting for DLL initialization...")
        local waitStart = os.clock()
        while readInteger(mb + MB.STATUS) == MB.STATUS_INIT do
            sleep(100)
            if (os.clock() - waitStart) * 1000 > CONFIG.POLL_TIMEOUT_MS then
                logErr("Timeout waiting for DLL init")
                return 0
            end
        end
    end

    -- ── Build mailbox command ──

    -- className
    writeStringToAddr(mb + MB.CLASS_NAME, CONFIG.GODOT_CLASS or "", 256)

    -- Filter: targetOp + targetValue = managed field value filter
    -- filterOp = managed field offset (repurposed)
    -- filterValue = read size (repurposed)
    if CONFIG.FILTER then
        writeInteger(mb + MB.TARGET_OP, opToFilterOp(CONFIG.FILTER.op))
        writeQword(mb + MB.TARGET_VALUE, CONFIG.FILTER.value or 0)
        writeInteger(mb + MB.FILTER_OP, CONFIG.FILTER.offset or 0)
        writeQword(mb + MB.FILTER_VALUE, CONFIG.FILTER.size or 4)
    else
        writeInteger(mb + MB.TARGET_OP, MB.FILTER_NONE)
        writeQword(mb + MB.TARGET_VALUE, 0)
        writeInteger(mb + MB.FILTER_OP, 0)
        writeQword(mb + MB.FILTER_VALUE, 4)
    end

    -- Write cmd LAST (triggers DLL-side scan)
    log("Sending CMD_FIND_MANAGED_INSTANCES (class='%s', filter offset=0x%X)...",
        CONFIG.GODOT_CLASS or "*",
        CONFIG.FILTER and CONFIG.FILTER.offset or 0)

    writeInteger(mb + MB.CMD, MB.CMD_FIND_MANAGED_INSTANCES)

    -- ── Poll for completion ──
    local pollStart = os.clock()
    while true do
        local st = readInteger(mb + MB.STATUS)
        if st == MB.STATUS_DONE then break end

        sleep(CONFIG.POLL_INTERVAL_MS)
        local elapsed = (os.clock() - pollStart) * 1000
        if elapsed > CONFIG.POLL_TIMEOUT_MS then
            logErr("Timeout waiting for DLL scan (%d ms)", math.floor(elapsed))
            return 0
        end
    end

    -- ── Read results ──
    local result = readInteger(mb + MB.RESULT)
    if result < 0 then
        local errMsg = readStringFromAddr(mb + MB.ERROR_MSG, 256)
        logErr("DLL scan failed: %s (code=%d)", errMsg, result)
        return 0
    end

    local count = readInteger(resBuf + RES.COUNT)
    if not count or count <= 0 then
        log("No matching .NET managed objects found")
        return 0
    end

    log("DLL scan returned %d matches, generating memory records...", count)

    -- ── Generate CE memory records from results buffer ──
    local matched = 0
    for i = 0, count - 1 do
        local entryBase = resBuf + RES.ENTRIES + i * RES.ENTRY_SIZE
        local objAddr = readQword(entryBase)
        local managedAddr = readQword(entryBase + 8)

        if managedAddr and managedAddr ~= 0 then
            matched = matched + 1

            -- Build descriptive label from field values
            local label = string.format("%s #%d", CONFIG.PREFIX, matched)
            local labelParts = {}
            for _, field in ipairs(CONFIG.FIELDS) do
                if field.ceType == 2 then -- int32
                    local val = readInteger(managedAddr + field.offset) or 0
                    table.insert(labelParts,
                        string.format("%s=%d", field.name, val))
                elseif field.ceType == 4 then -- float
                    local val = readFloat(managedAddr + field.offset) or 0
                    table.insert(labelParts,
                        string.format("%s=%.1f", field.name, val))
                end
            end
            if #labelParts > 0 then
                label = string.format("%s #%d (%s)",
                    CONFIG.PREFIX, matched, table.concat(labelParts, ", "))
            end

            -- Create a sub-group for this object
            local objGroup = getAddressList().createMemoryRecord()
            objGroup.Description = label
            objGroup.isGroupHeader = true
            objGroup.Options = "[moHideChildren]"
            if parentRecord then
                objGroup.appendToEntry(parentRecord)
            end
            table.insert(generatedRecords, objGroup)

            -- Create memory records for each display field
            for _, field in ipairs(CONFIG.FIELDS) do
                if shouldDisplay(field.name) then
                    local mr = getAddressList().createMemoryRecord()
                    mr.Description = string.format("%s.%s",
                        CONFIG.PREFIX, field.name)
                    mr.Address = string.format("%X",
                        managedAddr + field.offset)
                    mr.Type = field.ceType
                    mr.appendToEntry(objGroup)
                    table.insert(generatedRecords, mr)
                end
            end

            log("  [%d] Object=%s Managed=%s %s",
                matched,
                string.format("%X", objAddr),
                string.format("%X", managedAddr),
                label)
        end
    end

    log("Generated %d object groups with memory records", matched)
    return matched
end

-- ============================================================
-- Enable: Scan and create records
-- ============================================================

function zolt_managed_enable(parentEntry)
    log("=== .NET Managed Records: ENABLE ===")

    -- Create parent group
    groupRecord = getAddressList().createMemoryRecord()
    groupRecord.Description = CONFIG.GROUP_DESC
    groupRecord.isGroupHeader = true
    groupRecord.Color = 0x00BB5E
    if parentEntry then
        groupRecord.appendToEntry(parentEntry)
    end
    table.insert(generatedRecords, groupRecord)

    local matched = scanAndGenerate(groupRecord)

    if matched == 0 then
        groupRecord.Description = CONFIG.GROUP_DESC .. " (none found)"
    else
        groupRecord.Description = string.format("%s (%d found)",
            CONFIG.GROUP_DESC, matched)
    end

    log("=== Total %d memory records created ===", #generatedRecords)
end

-- ============================================================
-- Disable: Remove all generated records
-- ============================================================

function zolt_managed_disable()
    log("=== .NET Managed Records: DISABLE ===")

    -- Remove records in reverse order (children first)
    for i = #generatedRecords, 1, -1 do
        local mr = generatedRecords[i]
        if mr then
            pcall(function() mr.destroy() end)
        end
    end

    generatedRecords = {}
    groupRecord = nil
    log("All generated records removed")
end

-- ============================================================
-- Refresh: Re-scan and rebuild records (addresses may change)
-- ============================================================

function zolt_managed_refresh(parentEntry)
    zolt_managed_disable()
    zolt_managed_enable(parentEntry)
end

-- ============================================================
-- Register CE Lua function highlights
-- ============================================================

pcall(function()
    registerLuaFunctionHighlight('zolt_managed_enable')
    registerLuaFunctionHighlight('zolt_managed_disable')
    registerLuaFunctionHighlight('zolt_managed_refresh')
end)

-- ============================================================
-- CT Auto Assembler Entry Template
-- ============================================================
-- Copy the block below into a new CT entry's Auto Assembler script.
-- Place it as a child of [init] so zolt_log/zolt_callDLL are available.
--
--[[

[ENABLE]
{$lua}
if syntaxcheck then return end

-- Load the managed records module (adjust path as needed)
local scriptDir = extractFilePath(DLL_PATH or "")
local scriptPath = scriptDir .. "zolt_managed_records.lua"

-- Execute the script file to define CONFIG and functions
local f = io.open(scriptPath, "r")
if f then
    local code = f:read("*all")
    f:close()
    local fn, err = load(code)
    if fn then fn()
    else print("[Zolt-ManagedRec] Load error: " .. tostring(err)) end
else
    print("[Zolt-ManagedRec] Script not found: " .. scriptPath)
end

if zolt_managed_enable then
    zolt_managed_enable(nil)
end

-- Close Lua engine window after execution
closeLuaEngine()

[DISABLE]
{$lua}
if syntaxcheck then return end

if zolt_managed_disable then
    zolt_managed_disable()
end

]]--

-- ============================================================
-- Standalone test (run directly in CE Lua Engine window)
-- ============================================================
-- Uncomment the line below to test directly:
-- zolt_managed_enable(nil)
