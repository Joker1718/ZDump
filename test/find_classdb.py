"""
Find ClassDB::classes HashMap in Godot 4.4.1 binary.

ClassDB::classes is a HashMap<StringName, ClassInfo>.
After initialization, it should have:
- capacity > 0 (power of 2)
- num_elements > 0 (Godot has 800+ classes)
- entries pointer (non-null, heap)
- hashes pointer (non-null, heap)

Strategy: Find all LEA instructions that load .data/.bss addresses,
then check if any of them look like a HashMap with reasonable values.
"""
import struct
import sys

PE_PATH = r"D:\Github\Zolt-Dump\test\godot\Godot_v4.4.1-stable_win64.exe"

with open(PE_PATH, "rb") as f:
    data = f.read()

# Parse PE
dos_sig = struct.unpack_from("<H", data, 0)[0]
assert dos_sig == 0x5A4D, "Not a valid PE"
pe_off = struct.unpack_from("<I", data, 0x3C)[0]
nt_sig = struct.unpack_from("<I", data, pe_off)[0]
assert nt_sig == 0x4550, "Not a valid PE"

num_sections = struct.unpack_from("<H", data, pe_off + 6)[0]
opt_hdr_off = pe_off + 24
image_base = struct.unpack_from("<Q", data, opt_hdr_off + 24)[0]

sections = {}
sec_off = pe_off + 24 + 240  # PE64 optional header = 240 bytes
for i in range(num_sections):
    name_bytes = data[sec_off:sec_off+8]
    name = name_bytes.split(b'\x00')[0].decode('ascii', errors='replace')
    vsize = struct.unpack_from("<I", data, sec_off + 8)[0]
    rva = struct.unpack_from("<I", data, sec_off + 12)[0]
    raw_size = struct.unpack_from("<I", data, sec_off + 16)[0]
    raw_off = struct.unpack_from("<I", data, sec_off + 20)[0]
    chars = struct.unpack_from("<I", data, sec_off + 36)[0]
    writable = bool(chars & 0x80000000)

    sections[name] = {
        'rva': rva, 'vsize': vsize,
        'raw_off': raw_off, 'raw_size': raw_size,
        'writable': writable
    }
    print(f"  {name:8s}: RVA=0x{rva:08X} VSize=0x{vsize:08X} Raw=0x{raw_off:08X}+0x{raw_size:08X} W={writable}")
    sec_off += 40

text_sec = sections.get('.text', {})
data_sec = sections.get('.data', {})
bss_sec = sections.get('.bss', {})
rdata_sec = sections.get('.rdata', {})

text_rva = text_sec['rva']
text_size = text_sec['vsize']
text_raw = text_sec['raw_off']

# Writable sections
writable_ranges = []
for name, sec in sections.items():
    if sec['writable'] and sec['vsize'] > 0:
        writable_ranges.append((sec['rva'], sec['rva'] + sec['vsize'], name))
        print(f"  Writable: {name} RVA 0x{sec['rva']:08X} - 0x{sec['rva']+sec['vsize']:08X}")

def is_writable_rva(rva):
    for start, end, name in writable_ranges:
        if start <= rva < end:
            return True
    return False

# Scan .text for LEA rcx/rdx/rax, [rip+disp32] instructions that target writable sections
# 48 8D 0D = lea rcx, [rip+disp]
# 48 8D 15 = lea rdx, [rip+disp]
# 48 8D 05 = lea rax, [rip+disp]
# 4C 8D 05 = lea r8, [rip+disp]
# 4C 8D 0D = lea r9, [rip+disp]

lea_opcodes = [
    (b'\x48\x8D\x0D', 'lea rcx'),
    (b'\x48\x8D\x15', 'lea rdx'),
    (b'\x48\x8D\x05', 'lea rax'),
    (b'\x4C\x8D\x05', 'lea r8'),
    (b'\x4C\x8D\x0D', 'lea r9'),
    (b'\x4C\x8D\x15', 'lea r10'),
    (b'\x4C\x8D\x1D', 'lea r11'),
    (b'\x48\x8D\x35', 'lea rsi'),
    (b'\x48\x8D\x3D', 'lea rdi'),
]

# Map of RVA -> list of references
target_refs = {}

text_data = data[text_raw:text_raw + min(text_size, text_sec['raw_size'])]

for opcode_bytes, opcode_name in lea_opcodes:
    pos = 0
    while True:
        idx = text_data.find(opcode_bytes, pos)
        if idx == -1:
            break
        pos = idx + 1

        if idx + 7 > len(text_data):
            continue

        # RIP-relative: disp32 is at idx+3, instruction is 7 bytes
        disp = struct.unpack_from("<i", text_data, idx + 3)[0]
        instr_rva = text_rva + idx
        target_rva = instr_rva + 7 + disp

        if is_writable_rva(target_rva):
            if target_rva not in target_refs:
                target_refs[target_rva] = []
            target_refs[target_rva].append((instr_rva, opcode_name))

# Sort by reference count
sorted_targets = sorted(target_refs.items(), key=lambda x: -len(x[1]))

print(f"\n=== LEA targets in writable sections (top 30 by reference count) ===")
for target_rva, refs in sorted_targets[:30]:
    sec_name = "?"
    sec_offset = 0
    for name, sec in sections.items():
        if sec['rva'] <= target_rva < sec['rva'] + sec['vsize']:
            sec_name = name
            sec_offset = target_rva - sec['rva']
            break
    print(f"  RVA 0x{target_rva:08X} ({sec_name}+0x{sec_offset:06X}): {len(refs)} references")
    for ref_rva, ref_name in refs[:3]:
        print(f"    <- {ref_name} at RVA 0x{ref_rva:08X}")
    if len(refs) > 3:
        print(f"    ... and {len(refs)-3} more")

# Now also check ObjectDB address to confirm
# ObjectDB found at 00007FF6E6FC6208, module base 00007FF6DD980000
# So ObjectDB RVA = E6FC6208 - DD980000 = 0x9646208
objdb_rva = 0x9646208
print(f"\n=== Known addresses ===")
print(f"  ObjectDB RVA: 0x{objdb_rva:08X} ({target_refs.get(objdb_rva, []).__len__()} LEA refs)")

# Engine at E6DDB0A8 - DD980000 = 0x945B0A8
engine_rva = 0x945B0A8
print(f"  Engine RVA:   0x{engine_rva:08X} ({target_refs.get(engine_rva, []).__len__()} LEA refs)")

# SceneTree at E6DDB1E8 - DD980000 = 0x945B1E8
stree_rva = 0x945B1E8
print(f"  SceneTree RVA: 0x{stree_rva:08X} ({target_refs.get(stree_rva, []).__len__()} LEA refs)")

# Print top candidates in .bss that have many references and could be ClassDB
bss_rva = bss_sec['rva']
bss_end = bss_rva + bss_sec['vsize']
data_rva = data_sec['rva']
data_end = data_rva + data_sec['vsize']

print(f"\n=== Top LEA targets in .bss (likely global objects) ===")
bss_targets = [(rva, refs) for rva, refs in sorted_targets if bss_rva <= rva < bss_end]
for target_rva, refs in bss_targets[:20]:
    offset = target_rva - bss_rva
    print(f"  .bss+0x{offset:06X} (RVA 0x{target_rva:08X}): {len(refs)} refs")

print(f"\n=== Top LEA targets in .data ===")
data_targets = [(rva, refs) for rva, refs in sorted_targets if data_rva <= rva < data_end]
for target_rva, refs in data_targets[:20]:
    offset = target_rva - data_rva
    print(f"  .data+0x{offset:06X} (RVA 0x{target_rva:08X}): {len(refs)} refs")
