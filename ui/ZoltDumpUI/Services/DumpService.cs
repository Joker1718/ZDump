using System.Text.Json;
using System.Text.Json.Nodes;
using ZoltDumpUI.Core;
using ZoltDumpUI.Models;

namespace ZoltDumpUI.Services;

/// <summary>
/// Ausetiese — 服從之秤 (Aura's Obedience Scale)
/// Pipe command wrappers for Godot object inspection.
/// </summary>
public sealed class DumpService : IDumpService
{
    private readonly IPipeClient _pipe;

    public DumpService(IPipeClient pipe)
    {
        _pipe = pipe;
    }

    public async Task<EngineState> InitAsync(CancellationToken ct = default)
    {
        var resp = await _pipe.SendAsync("init", ct: ct);
        return new EngineState
        {
            Ok = resp["ok"]?.GetValue<bool>() ?? false,
            GodotVersion = resp["godot_version"]?.GetValue<string>() ?? "",
            ProcessName = resp["process_name"]?.GetValue<string>() ?? "",
            Pid = resp["pid"]?.GetValue<int>() ?? 0,
            ModulePath = resp["module_path"]?.GetValue<string>() ?? "",
            ScanMethod = resp["scan_method"]?.GetValue<string>() ?? "",
            Error = resp["error"]?.GetValue<string>() ?? ""
        };
    }

    public async Task<PointerState> GetPointersAsync(CancellationToken ct = default)
    {
        var resp = await _pipe.SendAsync("get_pointers", ct: ct);
        return new PointerState
        {
            ObjectDb = resp["object_db"]?.GetValue<string>() ?? "",
            ClassDb = resp["class_db"]?.GetValue<string>() ?? "",
            SceneTree = resp["scene_tree"]?.GetValue<string>() ?? "",
            Engine = resp["engine"]?.GetValue<string>() ?? "",
            SlotCount = resp["slot_count"]?.GetValue<int>() ?? 0,
            SlotMax = resp["slot_max"]?.GetValue<int>() ?? 0,
            ObjectCount = resp["object_count"]?.GetValue<int>() ?? 0,
            ObjectDbMethod = resp["object_db_method"]?.GetValue<string>() ?? "",
            ClassDbMethod = resp["class_db_method"]?.GetValue<string>() ?? "",
            SceneTreeMethod = resp["scene_tree_method"]?.GetValue<string>() ?? "",
            ObjectDbPatternId = resp["object_db_pattern_id"]?.GetValue<string>() ?? "",
            ClassDbPatternId = resp["class_db_pattern_id"]?.GetValue<string>() ?? "",
            SceneTreePatternId = resp["scene_tree_pattern_id"]?.GetValue<string>() ?? "",
            ObjectDbScanAddr = resp["object_db_scan_addr"]?.GetValue<string>() ?? "",
            ClassDbScanAddr = resp["class_db_scan_addr"]?.GetValue<string>() ?? "",
            SceneTreeScanAddr = resp["scene_tree_scan_addr"]?.GetValue<string>() ?? ""
        };
    }

    public async Task<int> GetObjectCountAsync(CancellationToken ct = default)
    {
        var resp = await _pipe.SendAsync("get_object_count", ct: ct);
        return resp["count"]?.GetValue<int>() ?? 0;
    }

    public async Task<ObjectListResult> GetObjectListAsync(int offset = 0, int limit = 500, CancellationToken ct = default)
    {
        var payload = new JsonObject { ["offset"] = offset, ["limit"] = limit };
        var resp = await _pipe.SendAsync("get_object_list", payload, ct);
        return ParseObjectList(resp);
    }

    public async Task<ObjectNode> GetObjectAsync(string address, CancellationToken ct = default)
    {
        var payload = new JsonObject { ["address"] = address };
        var resp = await _pipe.SendAsync("get_object", payload, ct);
        return ParseObjectNode(resp);
    }

    public async Task<ObjectNode?> FindObjectAsync(string nodePath, CancellationToken ct = default)
    {
        var payload = new JsonObject { ["path"] = nodePath };
        var resp = await _pipe.SendAsync("find_object", payload, ct);
        if (resp["found"]?.GetValue<bool>() != true) return null;
        return ParseObjectNode(resp);
    }

    public async Task<AddressLookupResult> FindByAddressAsync(string address, CancellationToken ct = default)
    {
        var payload = new JsonObject { ["address"] = address };
        var resp = await _pipe.SendAsync("find_by_address", payload, ct);

        // Parse alternatives (L2 managed_instance matches with multiple parent objects)
        var alts = new List<AlternativeEntry>();
        if (resp["alternatives"] is JsonArray altsArr)
        {
            foreach (var altNode in altsArr)
            {
                if (altNode is not JsonObject altObj) continue;
                alts.Add(new AlternativeEntry
                {
                    Address = altObj["address"]?.GetValue<string>() ?? "",
                    Name = altObj["name"]?.GetValue<string>() ?? "",
                    ClassName = altObj["class_name"]?.GetValue<string>() ?? "",
                    SlotIndex = altObj["slot_index"]?.GetValue<int>() ?? 0
                });
            }
        }

        return new AddressLookupResult
        {
            Found = resp["found"]?.GetValue<bool>() ?? false,
            MatchType = resp["match_type"]?.GetValue<string>() ?? "",
            Address = resp["address"]?.GetValue<string>() ?? "",
            SlotIndex = resp["slot_index"]?.GetValue<int>() ?? 0,
            Name = resp["name"]?.GetValue<string>() ?? "",
            ClassName = resp["class_name"]?.GetValue<string>() ?? "",
            ParentAddr = resp["parent_addr"]?.GetValue<string>() ?? "",
            OffsetFromBase = resp["member_offset"]?.GetValue<int>() ?? resp["offset_from_base"]?.GetValue<int>() ?? 0,
            QueryAddress = address,
            Alternatives = alts
        };
    }

    public async Task<ClassInfoModel> WalkClassAsync(string className, CancellationToken ct = default)
    {
        var payload = new JsonObject { ["class_name"] = className };
        var resp = await _pipe.SendAsync("walk_class", payload, ct);
        return ParseClassInfo(resp);
    }

    public async Task<InstanceWalkResult> WalkInstanceAsync(string address, CancellationToken ct = default)
    {
        var payload = new JsonObject { ["address"] = address };
        var resp = await _pipe.SendAsync("walk_instance", payload, ct);
        return ParseInstanceWalk(resp);
    }

    public async Task<ContainerWalkResult> WalkContainerAsync(string address, int variantType,
        int offset = 0, int limit = 500, CancellationToken ct = default)
    {
        var payload = new JsonObject
        {
            ["address"] = address,
            ["variant_type"] = variantType,
            ["offset"] = offset,
            ["limit"] = limit
        };
        var resp = await _pipe.SendAsync("walk_container", payload, ct);
        return ParseContainerWalk(resp);
    }

    public async Task<TreeWalkResult> WalkTreeAsync(int depth = -1, CancellationToken ct = default)
    {
        var payload = new JsonObject();
        if (depth >= 0) payload["depth"] = depth;
        var resp = await _pipe.SendAsync("walk_tree", payload, ct);
        return new TreeWalkResult
        {
            Root = resp["root"] is JsonObject rootObj ? ParseTreeNode(rootObj) : null
        };
    }

    public async Task<FindInstancesResult> FindInstancesAsync(string className, bool exactMatch = false, CancellationToken ct = default)
    {
        var payload = new JsonObject { ["class_name"] = className, ["exact"] = exactMatch, ["limit"] = 5000 };
        var resp = await _pipe.SendAsync("find_instances", payload, ct);
        var instances = new List<ObjectEntry>();
        if (resp["instances"] is JsonArray arr)
        {
            foreach (var item in arr)
            {
                if (item is JsonObject obj)
                    instances.Add(ParseObjectEntry(obj));
            }
        }
        return new FindInstancesResult
        {
            ClassName = resp["class_name"]?.GetValue<string>() ?? className,
            Count = resp["count"]?.GetValue<int>() ?? instances.Count,
            Instances = instances
        };
    }

    public async Task<List<ClassListItem>> ListClassesAsync(bool gameOnly = false, CancellationToken ct = default)
    {
        var payload = new JsonObject { ["game_only"] = gameOnly };
        var resp = await _pipe.SendAsync("list_classes", payload, ct);
        var result = new List<ClassListItem>();
        if (resp["classes"] is JsonArray arr)
        {
            foreach (var item in arr)
            {
                if (item is not JsonObject obj) continue;
                result.Add(new ClassListItem
                {
                    ClassName = obj["class_name"]?.GetValue<string>() ?? "",
                    SuperName = obj["super_name"]?.GetValue<string>() ?? "",
                    PropertyCount = obj["property_count"]?.GetValue<int>() ?? 0,
                    MethodCount = obj["method_count"]?.GetValue<int>() ?? 0,
                    IsGameClass = obj["is_game_class"]?.GetValue<bool>() ?? false,
                    Score = obj["score"]?.GetValue<int>() ?? 0
                });
            }
        }
        return result;
    }

    public async Task<PropertySearchResult> SearchPropertiesAsync(string query, bool gameOnly = false, CancellationToken ct = default)
    {
        var payload = new JsonObject { ["query"] = query, ["game_only"] = gameOnly };
        var resp = await _pipe.SendAsync("search_properties", payload, ct);
        var hits = new List<PropertySearchHit>();
        if (resp["hits"] is JsonArray arr)
        {
            foreach (var item in arr)
            {
                if (item is not JsonObject obj) continue;
                hits.Add(new PropertySearchHit
                {
                    ClassName = obj["class_name"]?.GetValue<string>() ?? "",
                    SuperName = obj["super_name"]?.GetValue<string>() ?? "",
                    PropertyName = obj["property_name"]?.GetValue<string>() ?? "",
                    PropertyType = obj["property_type"]?.GetValue<string>() ?? "",
                    Offset = obj["offset"]?.GetValue<int>() ?? 0,
                    Size = obj["size"]?.GetValue<int>() ?? 0,
                    Preview = obj["preview"]?.GetValue<string>() ?? ""
                });
            }
        }
        return new PropertySearchResult { Count = hits.Count, Hits = hits };
    }

    public async Task<byte[]> ReadMemAsync(string address, int size, CancellationToken ct = default)
    {
        var payload = new JsonObject { ["address"] = address, ["size"] = size };
        var resp = await _pipe.SendAsync("read_mem", payload, ct);
        var hex = resp["data"]?.GetValue<string>() ?? "";
        return Convert.FromHexString(hex);
    }

    public async Task<bool> WriteMemAsync(string address, byte[] data, CancellationToken ct = default)
    {
        var payload = new JsonObject { ["address"] = address, ["data"] = Convert.ToHexString(data) };
        var resp = await _pipe.SendAsync("write_mem", payload, ct);
        return resp["ok"]?.GetValue<bool>() ?? false;
    }

    public async Task<bool> WatchAsync(string address, CancellationToken ct = default)
    {
        var payload = new JsonObject { ["address"] = address };
        var resp = await _pipe.SendAsync("watch", payload, ct);
        return resp["ok"]?.GetValue<bool>() ?? false;
    }

    public async Task<bool> UnwatchAsync(string address, CancellationToken ct = default)
    {
        var payload = new JsonObject { ["address"] = address };
        var resp = await _pipe.SendAsync("unwatch", payload, ct);
        return resp["ok"]?.GetValue<bool>() ?? false;
    }

    public async Task<CePointerInfo> GetCePointerInfoAsync(string address, CancellationToken ct = default)
    {
        var payload = new JsonObject { ["address"] = address };
        var resp = await _pipe.SendAsync("get_ce_pointer_info", payload, ct);
        // TODO: Parse breadcrumbs and fields from response
        return new CePointerInfo();
    }

    public async Task<ScanStatusResult> GetScanStatusAsync(CancellationToken ct = default)
    {
        var resp = await _pipe.SendAsync("get_scan_status", ct: ct);
        return new ScanStatusResult
        {
            Status = resp["status"]?.GetValue<string>() ?? "",
            Progress = resp["progress"]?.GetValue<int>() ?? 0,
            Total = resp["total"]?.GetValue<int>() ?? 0,
            Message = resp["message"]?.GetValue<string>() ?? "",
            IsComplete = resp["is_complete"]?.GetValue<bool>() ?? false
        };
    }

    public async Task TriggerScanAsync(CancellationToken ct = default)
    {
        await _pipe.SendAsync("trigger_scan", ct: ct);
    }

    // --- Parsing helpers ---

    private static ObjectListResult ParseObjectList(JsonObject resp)
    {
        var objects = new List<ObjectEntry>();
        if (resp["objects"] is JsonArray arr)
        {
            foreach (var item in arr)
            {
                if (item is JsonObject obj)
                    objects.Add(ParseObjectEntry(obj));
            }
        }
        return new ObjectListResult
        {
            Total = resp["total"]?.GetValue<int>() ?? 0,
            Offset = resp["offset"]?.GetValue<int>() ?? 0,
            Count = objects.Count,
            Scanned = resp["scanned"]?.GetValue<int>() ?? 0,
            Objects = objects
        };
    }

    private static ObjectEntry ParseObjectEntry(JsonObject obj) => new()
    {
        Address = obj["address"]?.GetValue<string>() ?? "",
        Name = obj["name"]?.GetValue<string>() ?? "",
        ClassName = obj["class_name"]?.GetValue<string>() ?? "",
        SlotIndex = obj["slot_index"]?.GetValue<int>() ?? 0,
        ObjectId = obj["object_id"]?.GetValue<long>() ?? 0,
        ParentAddr = obj["parent_addr"]?.GetValue<string>() ?? ""
    };

    private static ObjectNode ParseObjectNode(JsonObject resp) => new()
    {
        Address = resp["address"]?.GetValue<string>() ?? "",
        Name = resp["name"]?.GetValue<string>() ?? "",
        ClassName = resp["class_name"]?.GetValue<string>() ?? "",
        ClassAddr = resp["class_addr"]?.GetValue<string>() ?? "",
        ParentAddr = resp["parent_addr"]?.GetValue<string>() ?? "",
        ParentName = resp["parent_name"]?.GetValue<string>() ?? "",
        NodePath = resp["node_path"]?.GetValue<string>() ?? "",
        ObjectId = resp["object_id"]?.GetValue<long>() ?? 0,
        SlotIndex = resp["slot_index"]?.GetValue<int>() ?? 0,
        IsNode = resp["is_node"]?.GetValue<bool>() ?? false,
        IsRefCounted = resp["is_ref_counted"]?.GetValue<bool>() ?? false
    };

    private static ClassInfoModel ParseClassInfo(JsonObject resp)
    {
        var props = new List<FieldInfoModel>();
        if (resp["properties"] is JsonArray propArr)
        {
            foreach (var item in propArr)
            {
                if (item is not JsonObject obj) continue;
                props.Add(new FieldInfoModel
                {
                    Name = obj["name"]?.GetValue<string>() ?? "",
                    Type = obj["type"]?.GetValue<string>() ?? "",
                    Offset = obj["offset"]?.GetValue<int>() ?? 0,
                    Size = obj["size"]?.GetValue<int>() ?? 0,
                    BitShift = obj["bit_shift"]?.GetValue<int>() ?? 0,
                    BitMask = obj["bit_mask"]?.GetValue<int>() ?? 0,
                    Hint = obj["hint"]?.GetValue<int>() ?? 0,
                    HintString = obj["hint_string"]?.GetValue<string>() ?? "",
                    Usage = obj["usage"]?.GetValue<int>() ?? 0
                });
            }
        }
        var methods = new List<MethodInfoEntry>();
        if (resp["methods"] is JsonArray methArr)
        {
            foreach (var item in methArr)
            {
                if (item is not JsonObject obj) continue;
                var args = new List<MethodArgEntry>();
                if (obj["args"] is JsonArray argArr)
                {
                    foreach (var a in argArr)
                    {
                        if (a is not JsonObject ao) continue;
                        args.Add(new MethodArgEntry
                        {
                            Name = ao["name"]?.GetValue<string>() ?? "",
                            Type = ao["type"]?.GetValue<string>() ?? ""
                        });
                    }
                }
                methods.Add(new MethodInfoEntry
                {
                    Name = obj["name"]?.GetValue<string>() ?? "",
                    ReturnType = obj["return_type"]?.GetValue<string>() ?? "",
                    Address = obj["address"]?.GetValue<string>() ?? "",
                    Args = args,
                    Flags = obj["flags"]?.GetValue<int>() ?? 0
                });
            }
        }
        return new ClassInfoModel
        {
            ClassName = resp["class_name"]?.GetValue<string>() ?? "",
            SuperName = resp["super_name"]?.GetValue<string>() ?? "",
            ClassAddr = resp["class_addr"]?.GetValue<string>() ?? "",
            PropertiesCount = props.Count,
            MethodCount = methods.Count,
            Properties = props,
            Methods = methods
        };
    }

    private static InstanceWalkResult ParseInstanceWalk(JsonObject resp)
    {
        var fields = new List<LiveFieldValue>();
        if (resp["fields"] is JsonArray arr)
        {
            foreach (var item in arr)
            {
                if (item is JsonObject obj)
                    fields.Add(ParseLiveField(obj));
            }
        }
        return new InstanceWalkResult
        {
            Address = resp["address"]?.GetValue<string>() ?? "",
            Name = resp["name"]?.GetValue<string>() ?? "",
            ClassName = resp["class_name"]?.GetValue<string>() ?? "",
            ParentAddr = resp["parent_addr"]?.GetValue<string>() ?? "",
            IsNode = resp["is_node"]?.GetValue<bool>() ?? false,
            Fields = fields
        };
    }

    private static ContainerWalkResult ParseContainerWalk(JsonObject resp)
    {
        var fields = new List<LiveFieldValue>();
        if (resp["fields"] is JsonArray arr)
        {
            foreach (var item in arr)
            {
                if (item is JsonObject obj)
                    fields.Add(ParseLiveField(obj));
            }
        }
        return new ContainerWalkResult
        {
            Address = resp["address"]?.GetValue<string>() ?? "",
            ContainerType = resp["container_type"]?.GetValue<string>() ?? "",
            ElementCount = resp["element_count"]?.GetValue<int>() ?? 0,
            ElementType = resp["element_type"]?.GetValue<string>() ?? "",
            KeyType = resp["key_type"]?.GetValue<string>() ?? "",
            Fields = fields
        };
    }

    private static LiveFieldValue ParseLiveField(JsonObject obj)
    {
        var subFields = new List<LiveFieldValue>();
        if (obj["sub_fields"] is JsonArray subArr)
        {
            foreach (var s in subArr)
            {
                if (s is JsonObject so)
                    subFields.Add(ParseLiveField(so));
            }
        }
        var enumValues = new List<string>();
        if (obj["enum_values"] is JsonArray enumArr)
        {
            foreach (var e in enumArr)
                enumValues.Add(e?.GetValue<string>() ?? "");
        }
        return new LiveFieldValue
        {
            Name = obj["name"]?.GetValue<string>() ?? "",
            Type = obj["type"]?.GetValue<string>() ?? "",
            Value = obj["value"]?.GetValue<string>() ?? "",
            Hex = obj["hex"]?.GetValue<string>() ?? "",
            Address = obj["address"]?.GetValue<string>() ?? "",
            Offset = obj["offset"]?.GetValue<int>() ?? 0,
            ClassName = obj["class_name"]?.GetValue<string>() ?? "",
            Size = obj["size"]?.GetValue<int>() ?? 0,
            BitShift = obj["bit_shift"]?.GetValue<int>() ?? 0,
            BitMask = obj["bit_mask"]?.GetValue<int>() ?? 0,
            PtrValue = obj["ptr_value"]?.GetValue<string>() ?? "",
            PtrClass = obj["ptr_class"]?.GetValue<string>() ?? "",
            IsPointer = obj["is_pointer"]?.GetValue<bool>() ?? false,
            IsStruct = obj["is_struct"]?.GetValue<bool>() ?? false,
            IsContainer = obj["is_container"]?.GetValue<bool>() ?? false,
            IsEditable = obj["is_editable"]?.GetValue<bool>() ?? false,
            ElementCount = obj["element_count"]?.GetValue<int>() ?? 0,
            ElementType = obj["element_type"]?.GetValue<string>() ?? "",
            KeyType = obj["key_type"]?.GetValue<string>() ?? "",
            VariantType = obj["variant_type"]?.GetValue<int>() ?? 0,
            Hint = obj["hint"]?.GetValue<int>() ?? 0,
            HintString = obj["hint_string"]?.GetValue<string>() ?? "",
            Usage = obj["usage"]?.GetValue<int>() ?? 0,
            EnumName = obj["enum_name"]?.GetValue<string>() ?? "",
            EnumValues = enumValues,
            SubFields = subFields
        };
    }

    private static TreeNodeInfo ParseTreeNode(JsonObject obj)
    {
        var children = new List<TreeNodeInfo>();
        if (obj["children"] is JsonArray arr)
        {
            foreach (var item in arr)
            {
                if (item is JsonObject child)
                    children.Add(ParseTreeNode(child));
            }
        }
        return new TreeNodeInfo
        {
            Address = obj["address"]?.GetValue<string>() ?? "",
            Name = obj["name"]?.GetValue<string>() ?? "",
            ClassName = obj["class_name"]?.GetValue<string>() ?? "",
            NodePath = obj["node_path"]?.GetValue<string>() ?? "",
            ChildCount = obj["child_count"]?.GetValue<int>() ?? children.Count,
            Children = children
        };
    }
}
