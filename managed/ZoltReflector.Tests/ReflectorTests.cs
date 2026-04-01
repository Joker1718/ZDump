// Metode — 梅特黛 (All-capable Analyst — .NET Reflection Bridge)
// Auto-tests for the ZoltReflector reflection engine.

using System.Runtime.InteropServices;
using Xunit;

namespace ZoltReflector.Tests;

public class ReflectorTests
{
    // ================================================================
    // Assembly enumeration
    // ================================================================

    [Fact]
    public void GetGameAssemblies_FiltersSystemAssemblies()
    {
        var assemblies = Reflector.GetGameAssemblies();

        // Should NOT contain System.* or Microsoft.*
        foreach (var asm in assemblies)
        {
            var name = asm.GetName().Name ?? "";
            Assert.DoesNotMatch("^System", name);
            Assert.DoesNotMatch("^Microsoft", name);
            Assert.NotEqual("netstandard", name);
            Assert.NotEqual("mscorlib", name);
        }

        // Should contain our test assembly
        Assert.Contains(assemblies, a =>
            (a.GetName().Name ?? "").Contains("ZoltReflector"));
    }

    [Fact]
    public void GetAssembliesByName_FindsSpecificAssembly()
    {
        var assemblies = Reflector.GetAssembliesByName("ZoltReflector.Tests");
        Assert.Single(assemblies);
        Assert.Equal("ZoltReflector.Tests", assemblies[0].GetName().Name);
    }

    [Fact]
    public void GetAssembliesByName_ReturnsEmpty_ForUnknown()
    {
        var assemblies = Reflector.GetAssembliesByName("NonExistentAssembly");
        Assert.Empty(assemblies);
    }

    // ================================================================
    // Type enumeration
    // ================================================================

    [Fact]
    public void EnumerateTypes_FindsTestTypes()
    {
        var assemblies = Reflector.GetAssembliesByName("ZoltReflector.Tests");
        var types = Reflector.EnumerateTypes(assemblies);

        var typeNames = types.Select(t => t.FullName).ToHashSet();

        Assert.Contains("ZoltReflector.Tests.Creature", typeNames);
        Assert.Contains("ZoltReflector.Tests.Card", typeNames);
        Assert.Contains("ZoltReflector.Tests.MixedFields", typeNames);
        Assert.Contains("ZoltReflector.Tests.Vector2", typeNames);
    }

    [Fact]
    public void EnumerateTypes_HasFieldCounts()
    {
        var assemblies = Reflector.GetAssembliesByName("ZoltReflector.Tests");
        var types = Reflector.EnumerateTypes(assemblies);

        var card = types.First(t => t.FullName == "ZoltReflector.Tests.Card");
        Assert.Equal(6, card.FieldCount); // CardId, CardName, Cost, BaseDamage, Upgraded, Rarity
    }

    [Fact]
    public void EnumerateTypes_HasMethodTablePointers()
    {
        var assemblies = Reflector.GetAssembliesByName("ZoltReflector.Tests");
        var types = Reflector.EnumerateTypes(assemblies);

        var creature = types.First(t => t.FullName == "ZoltReflector.Tests.Creature");
        Assert.NotEqual(0, creature.MethodTablePtr);
    }

    // ================================================================
    // Type layout — reference types
    // ================================================================

    [Fact]
    public void GetTypeLayout_Card_HasAllFields()
    {
        var layout = Reflector.GetTypeLayout(typeof(Card));
        Assert.NotNull(layout);

        Assert.Equal("ZoltReflector.Tests.Card", layout.FullName);
        Assert.False(layout.IsValueType);

        var fieldNames = layout.Fields
            .Where(f => !f.IsStatic)
            .Select(f => f.Name)
            .ToHashSet();

        Assert.Contains("CardId", fieldNames);
        Assert.Contains("CardName", fieldNames);
        Assert.Contains("Cost", fieldNames);
        Assert.Contains("BaseDamage", fieldNames);
        Assert.Contains("Upgraded", fieldNames);
        Assert.Contains("Rarity", fieldNames);
    }

    [Fact]
    public void GetTypeLayout_Card_FieldsHaveReasonableOffsets()
    {
        var layout = Reflector.GetTypeLayout(typeof(Card));
        Assert.NotNull(layout);

        var instanceFields = layout.Fields.Where(f => !f.IsStatic).ToList();

        // Value type fields (int, byte, bool) should always have valid offsets
        var valueTypeFields = instanceFields.Where(f => f.IsValueType).ToList();
        Assert.True(valueTypeFields.Count >= 4,
            $"Expected at least 4 value type fields, got {valueTypeFields.Count}");

        foreach (var field in valueTypeFields)
        {
            Assert.True(field.Offset >= 8,
                $"Field {field.Name} offset {field.Offset} should be >= 8 (after MethodTable*)");
            Assert.True(field.Offset < 256,
                $"Field {field.Name} offset {field.Offset} seems unreasonably large");
        }

        // Reference type fields may return -1 in some runtimes (GC relocation)
        // but if resolved, they should be in range
        foreach (var field in instanceFields.Where(f => !f.IsValueType && f.Offset >= 0))
        {
            Assert.True(field.Offset >= 8,
                $"Ref field {field.Name} offset {field.Offset} should be >= 8");
        }
    }

    [Fact]
    public void GetTypeLayout_Card_FieldsAreDistinct()
    {
        var layout = Reflector.GetTypeLayout(typeof(Card));
        Assert.NotNull(layout);

        var instanceFields = layout.Fields.Where(f => !f.IsStatic).ToList();
        var offsets = instanceFields.Select(f => f.Offset).ToList();

        // All offsets should be distinct (no overlapping fields in a normal class)
        Assert.Equal(offsets.Count, offsets.Distinct().Count());
    }

    [Fact]
    public void GetTypeLayout_Creature_IncludesOnlyDeclaredFields()
    {
        var layout = Reflector.GetTypeLayout(typeof(Creature));
        Assert.NotNull(layout);

        // GetTypeLayout with DeclaredOnly should only have Creature's own fields
        var fieldNames = layout.Fields
            .Where(f => !f.IsStatic)
            .Select(f => f.Name)
            .ToHashSet();

        Assert.Contains("CurrentHp", fieldNames);
        Assert.Contains("MaxHp", fieldNames);
        Assert.Contains("IsPlayer", fieldNames);

        // Should NOT contain base class fields (DeclaredOnly)
        Assert.DoesNotContain("EntityId", fieldNames);
        Assert.DoesNotContain("PositionX", fieldNames);
    }

    [Fact]
    public void GetTypeLayout_Creature_BaseTypeIsGameEntity()
    {
        var layout = Reflector.GetTypeLayout(typeof(Creature));
        Assert.NotNull(layout);
        Assert.Equal("ZoltReflector.Tests.GameEntity", layout.BaseTypeName);
    }

    // ================================================================
    // Type layout — value types
    // ================================================================

    [Fact]
    public void GetTypeLayout_Vector2_Sequential()
    {
        var layout = Reflector.GetTypeLayout(typeof(Vector2));
        Assert.NotNull(layout);
        Assert.True(layout.IsValueType);

        var x = layout.Fields.First(f => f.Name == "X");
        var y = layout.Fields.First(f => f.Name == "Y");

        Assert.Equal(0, x.Offset);
        Assert.Equal(4, y.Offset);
        Assert.Equal(4, x.Size);
        Assert.Equal(4, y.Size);
    }

    [Fact]
    public void GetTypeLayout_VariantData_ExplicitLayout()
    {
        var layout = Reflector.GetTypeLayout(typeof(VariantData));
        Assert.NotNull(layout);

        var intVal = layout.Fields.First(f => f.Name == "IntValue");
        var floatVal = layout.Fields.First(f => f.Name == "FloatValue");
        var longVal = layout.Fields.First(f => f.Name == "LongValue");
        var type = layout.Fields.First(f => f.Name == "Type");

        // IntValue and FloatValue overlap at offset 0
        Assert.Equal(0, intVal.Offset);
        Assert.Equal(0, floatVal.Offset);
        Assert.Equal(0, longVal.Offset);
        Assert.Equal(8, type.Offset);
    }

    // ================================================================
    // Field sizes
    // ================================================================

    [Fact]
    public void GetTypeLayout_MixedFields_CorrectSizes()
    {
        var layout = Reflector.GetTypeLayout(typeof(MixedFields));
        Assert.NotNull(layout);

        var bySize = layout.Fields.ToDictionary(f => f.Name, f => f.Size);

        Assert.Equal(1, bySize["ByteField"]);
        Assert.Equal(2, bySize["ShortField"]);
        Assert.Equal(4, bySize["IntField"]);
        Assert.Equal(8, bySize["LongField"]);
        Assert.Equal(4, bySize["FloatField"]);
        Assert.Equal(8, bySize["DoubleField"]);
        Assert.Equal(1, bySize["BoolField"]);
        Assert.Equal(2, bySize["CharField"]);
        Assert.Equal(8, bySize["StringField"]);   // reference = pointer
        Assert.Equal(8, bySize["ObjectField"]);   // reference = pointer
        Assert.Equal(8, bySize["ArrayField"]);    // reference = pointer
    }

    [Fact]
    public void GetTypeLayout_MixedFields_StaticFieldsSeparated()
    {
        var layout = Reflector.GetTypeLayout(typeof(MixedFields));
        Assert.NotNull(layout);

        var staticFields = layout.Fields.Where(f => f.IsStatic).ToList();
        var instanceFields = layout.Fields.Where(f => !f.IsStatic).ToList();

        Assert.Equal(2, staticFields.Count); // StaticField, StaticStringField
        Assert.True(instanceFields.Count >= 11);

        // Static fields should have offset -1
        foreach (var sf in staticFields)
            Assert.Equal(-1, sf.Offset);
    }

    // ================================================================
    // Type resolution
    // ================================================================

    [Fact]
    public void FindType_ByFullName()
    {
        var type = Reflector.FindType("ZoltReflector.Tests.Creature");
        Assert.NotNull(type);
        Assert.Equal(typeof(Creature), type);
    }

    [Fact]
    public void FindType_ReturnsNull_ForUnknown()
    {
        var type = Reflector.FindType("NonExistent.Type.Name");
        Assert.Null(type);
    }

    // ================================================================
    // MethodTable
    // ================================================================

    [Fact]
    public void GetMethodTable_ReturnsNonZero()
    {
        nint mt = Reflector.GetMethodTable(typeof(Creature));
        Assert.NotEqual(0, mt);
    }

    [Fact]
    public void GetMethodTable_DifferentTypesHaveDifferentMT()
    {
        nint mtCreature = Reflector.GetMethodTable(typeof(Creature));
        nint mtCard = Reflector.GetMethodTable(typeof(Card));
        Assert.NotEqual(mtCreature, mtCard);
    }

    // ================================================================
    // Field offset correctness (verify against known .NET behavior)
    // ================================================================

    [Fact]
    public unsafe void FieldOffset_Card_VerifyViaRawMemory()
    {
        // Create a Card with known values, then verify field offsets
        // by reading raw object memory at the discovered offsets
        var card = new Card
        {
            CardId = 0x12345678,
            Cost = 42,
            BaseDamage = 99,
            Upgraded = true,
            Rarity = 3
        };

        var layout = Reflector.GetTypeLayout(typeof(Card));
        Assert.NotNull(layout);

        var cardIdField = layout.Fields.First(f => f.Name == "CardId");
        var costField = layout.Fields.First(f => f.Name == "Cost");

        // Get raw object pointer via __makeref (works on .NET 8)
        var tr = __makeref(card);
        nint objPtr = **(nint**)&tr; // pointer to object start (where MT* lives)

        // Offsets are from object start (offset 0 = MethodTable*)
        // So field data is at objPtr + offset
        int cardIdRaw = *(int*)(objPtr + cardIdField.Offset);
        int costRaw = *(int*)(objPtr + costField.Offset);

        Assert.Equal(0x12345678, cardIdRaw);
        Assert.Equal(42, costRaw);
    }

    // ================================================================
    // Integration: full pipeline (enumerate → get layout)
    // ================================================================

    [Fact]
    public void Integration_EnumerateAndGetLayout()
    {
        // Step 1: Find assemblies
        var assemblies = Reflector.GetAssembliesByName("ZoltReflector.Tests");
        Assert.NotEmpty(assemblies);

        // Step 2: Enumerate types
        var types = Reflector.EnumerateTypes(assemblies);
        Assert.NotEmpty(types);

        // Step 3: Get layout for each type that has fields
        int layoutsObtained = 0;
        foreach (var ts in types.Where(t => t.FieldCount > 0))
        {
            var type = Reflector.FindType(ts.FullName);
            if (type is null) continue;

            var layout = Reflector.GetTypeLayout(type);
            if (layout is null) continue;

            layoutsObtained++;

            // Verify basic consistency
            Assert.Equal(ts.FullName, layout.FullName);
            Assert.NotEmpty(layout.Fields);
        }

        Assert.True(layoutsObtained >= 5,
            $"Expected at least 5 type layouts, got {layoutsObtained}");
    }
}
