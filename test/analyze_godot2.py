import struct

GODOT_PATH = r"D:\Github\Zolt-Dump\test\godot\Godot_v4.4.1-stable_win64.exe"

with open(GODOT_PATH, "rb") as f:
    data = f.read()

# Parse PE headers
e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
coff_offset = e_lfanew + 4
num_sections = struct.unpack_from("<H", data, coff_offset + 2)[0]
optional_hdr_size = struct.unpack_from("<H", data, coff_offset + 16)[0]
opt_offset = coff_offset + 20

sections_offset = opt_offset + optional_hdr_size
sections = {}
for i in range(num_sections):
    sec_off = sections_offset + i * 40
    name = data[sec_off:sec_off+8].split(b'\x00')[0].decode('ascii', errors='replace')
    virtual_size = struct.unpack_from("<I", data, sec_off + 8)[0]
    virtual_addr = struct.unpack_from("<I", data, sec_off + 12)[0]
    raw_size = struct.unpack_from("<I", data, sec_off + 16)[0]
    raw_ptr = struct.unpack_from("<I", data, sec_off + 20)[0]
    sections[name] = {'vaddr': virtual_addr, 'vsize': virtual_size, 'rptr': raw_ptr, 'rsize': raw_size}

text_sec = sections['.text']
data_sec = sections['.data']
bss_sec = sections['.bss']

text_data = data[text_sec['rptr']:text_sec['rptr']+text_sec['rsize']]

def rva_to_offset(rva):
    for s in sections.values():
        if s['rptr'] > 0 and s['vaddr'] <= rva < s['vaddr'] + s['rsize']:
            return s['rptr'] + (rva - s['vaddr'])
    return None

def is_writable_rva(rva):
    return ((data_sec['vaddr'] <= rva < data_sec['vaddr'] + data_sec['vsize']) or
            (bss_sec['vaddr'] <= rva < bss_sec['vaddr'] + bss_sec['vsize']))

# ============================================================
# Part 1: ALL singleton-style getters targeting writable sections
# ============================================================
print("=" * 70)
print("ALL 'mov rax,[rip+disp]; ret' with targets in writable sections")
print("=" * 70)

all_getters = []
for i in range(len(text_data) - 8):
    if (text_data[i] == 0x48 and text_data[i+1] == 0x8B and
        text_data[i+2] == 0x05 and text_data[i+7] == 0xC3):
        instr_rva = text_sec['vaddr'] + i
        disp = struct.unpack_from("<i", text_data, i+3)[0]
        target_rva = instr_rva + 7 + disp
        if is_writable_rva(target_rva):
            all_getters.append((i, instr_rva, target_rva))

print(f"Total: {len(all_getters)} getters targeting writable sections\n")

for idx, (off, rva, trva) in enumerate(all_getters):
    section_name = ".data" if data_sec['vaddr'] <= trva < data_sec['vaddr'] + data_sec['vsize'] else ".bss"
    sec_offset = trva - data_sec['vaddr'] if section_name == ".data" else trva - bss_sec['vaddr']

    ctx_start = max(0, off - 8)
    ctx_end = min(len(text_data), off + 24)
    ctx = text_data[ctx_start:ctx_end]
    ctx_hex = ' '.join(f'{b:02X}' for b in ctx)

    after_ret = text_data[off+8:off+16] if off+16 < len(text_data) else b''
    after_hex = ' '.join(f'{b:02X}' for b in after_ret)

    # Read value at target in .data
    val_str = ""
    if section_name == ".data":
        file_off = rva_to_offset(trva)
        if file_off and file_off + 8 <= len(data):
            val = struct.unpack_from("<Q", data, file_off)[0]
            val_str = f" val={hex(val)}"

    print(f"  [{idx}] RVA={hex(rva)} -> {section_name}+{hex(sec_offset)} (RVA={hex(trva)}){val_str}")
    print(f"      Before+instr+after: {ctx_hex}")
    print(f"      After ret: {after_hex}")
    print()

# ============================================================
# Part 2: ObjectDB slot calculation (shl by 4)
# ============================================================
print("=" * 70)
print("ObjectDB slot patterns (shl ?, 4 near mov [rip+disp])")
print("=" * 70)

shl4_matches = []
for i in range(len(text_data) - 4):
    if (text_data[i] == 0x48 and text_data[i+1] == 0xC1 and
        text_data[i+3] == 0x04 and (text_data[i+2] & 0xF8) == 0xE0):
        shl4_matches.append(i)

print(f"Total 'shl reg64, 4' instructions: {len(shl4_matches)}")

hits = []
for shl_off in shl4_matches:
    for search_back in range(30):
        j = shl_off - 7 - search_back
        if j < 0: break
        if j+7 <= len(text_data) and text_data[j] == 0x48 and text_data[j+1] == 0x8B:
            mod_rm = text_data[j+2]
            if (mod_rm & 0xC7) == 0x05:  # [rip+disp32] addressing
                instr_rva = text_sec['vaddr'] + j
                disp = struct.unpack_from("<i", text_data, j+3)[0]
                target_rva = instr_rva + 7 + disp
                if is_writable_rva(target_rva):
                    ctx_start = max(0, j - 4)
                    ctx_end = min(len(text_data), shl_off + 8)
                    ctx = text_data[ctx_start:ctx_end]
                    ctx_hex = ' '.join(f'{b:02X}' for b in ctx)
                    section_name = ".data" if data_sec['vaddr'] <= target_rva < data_sec['vaddr'] + data_sec['vsize'] else ".bss"
                    sec_off = target_rva - data_sec['vaddr'] if section_name == ".data" else target_rva - bss_sec['vaddr']
                    hits.append((j, instr_rva, target_rva, shl_off, ctx_hex, section_name, sec_off))
                    break

print(f"Matches near 'mov reg,[rip+disp]' targeting writable sections: {len(hits)}")
for h in hits[:20]:
    j, irva, trva, shl_off, ctx, sec, sec_off = h
    print(f"  RVA={hex(irva)} -> {sec}+{hex(sec_off)} (RVA={hex(trva)}), shl at +{shl_off-j}")
    print(f"    Bytes: {ctx}")

# ============================================================
# Part 3: Constructor patterns (mov [rip+disp], rcx)
# ============================================================
print("\n" + "=" * 70)
print("Constructor patterns: mov [rip+disp], rcx -> writable sections")
print("=" * 70)

store_this = []
for i in range(len(text_data) - 7):
    if (text_data[i] == 0x48 and text_data[i+1] == 0x89 and text_data[i+2] == 0x0D):
        instr_rva = text_sec['vaddr'] + i
        disp = struct.unpack_from("<i", text_data, i+3)[0]
        target_rva = instr_rva + 7 + disp
        if is_writable_rva(target_rva):
            store_this.append((i, instr_rva, target_rva))

print(f"Total 'mov [rip+disp], rcx' to writable sections: {len(store_this)}")

getter_targets = set(g[2] for g in all_getters)
print(f"\nCross-references (store targets matching getter targets):")
for st in store_this:
    if st[2] in getter_targets:
        off, rva, trva = st
        section_name = ".data" if data_sec['vaddr'] <= trva < data_sec['vaddr'] + data_sec['vsize'] else ".bss"
        sec_off = trva - data_sec['vaddr'] if section_name == ".data" else trva - bss_sec['vaddr']
        ctx = text_data[max(0,off-4):min(len(text_data),off+20)]
        ctx_hex = ' '.join(f'{b:02X}' for b in ctx)
        print(f"  XREF RVA={hex(rva)} -> {section_name}+{hex(sec_off)} (RVA={hex(trva)})")
        print(f"    Bytes: {ctx_hex}")

# ============================================================
# Part 4: PE Exports
# ============================================================
print("\n" + "=" * 70)
print("PE Exports")
print("=" * 70)

edata_sec = sections.get('.edata')
if edata_sec and edata_sec['rptr'] > 0:
    edata_off = edata_sec['rptr']
    num_funcs = struct.unpack_from("<I", data, edata_off + 20)[0]
    num_names = struct.unpack_from("<I", data, edata_off + 24)[0]
    addr_of_names_rva = struct.unpack_from("<I", data, edata_off + 32)[0]
    print(f"Num functions: {num_funcs}, Num names: {num_names}")

    names_off = rva_to_offset(addr_of_names_rva)
    if names_off:
        for i in range(min(num_names, 50)):
            name_rva = struct.unpack_from("<I", data, names_off + i*4)[0]
            name_off = rva_to_offset(name_rva)
            if name_off:
                name = b''
                while name_off < len(data) and data[name_off] != 0:
                    name += bytes([data[name_off]])
                    name_off += 1
                print(f"  {name.decode('ascii', errors='replace')}")
