"""Gera o AgeOfSkyrim.esp mínimo sem o Creation Kit (ADR 0034).

Conteúdo (nada vanilla é editado; o plugin só ADICIONA forms novos):
  - TES4: ESL (flag 0x200), HEDR 1.71, master Skyrim.esm.
  - QUST "AoS_MCMQuest" (0x800): Start Game Enabled, script AgeOfSkyrimMCM (SkyUI) no VMAD e um alias do
    jogador (forçado em PlayerRef 0x14) com SKI_PlayerLoadGameAlias, para o SkyUI receber OnGameReload.

Sem SkyUI os scripts não existem em jogo: o Papyrus só registra um aviso no log e o mod funciona sem MCM.

Formato: https://en.uesp.net/wiki/Skyrim_Mod:Mod_File_Format (confere com as definições do xEdit, TES5).
Para crescer (ex.: base objects da sede na M6), acrescente records em RECORDS com FormIDs locais novos
(0x801, 0x802... dentro da faixa ESL 0x800-0xFFF) e suba NEXT_OBJECT_ID.

Uso: python tools/build_esp.py <saída.esp>
"""
import struct
import sys

PLUGIN_NAME = "AgeOfSkyrim.esp"
MASTERS = ["Skyrim.esm"]
AUTHOR = "Leonardo"
DESCRIPTION = "Age of Skyrim - MCM (gerado por tools/build_esp.py)"

FORM_VERSION = 44          # Skyrim SE
HEDR_VERSION = 1.71        # SE com faixa ESL estendida
TES4_FLAG_ESL = 0x00000200
QUST_FLAG_START_GAME_ENABLED = 0x0001

SELF_INDEX = len(MASTERS)  # FormIDs do próprio plugin usam o índice = nº de masters
PLAYER_REF = 0x00000014    # Skyrim.esm (índice 0) PlayerRef

MCM_QUEST_LOCAL_ID = 0x800
NEXT_OBJECT_ID = 0x801

MCM_SCRIPT = "AgeOfSkyrimMCM"
PLAYER_ALIAS_SCRIPT = "SKI_PlayerLoadGameAlias"
PLAYER_ALIAS_NAME = "PlayerAlias"
PLAYER_ALIAS_ID = 0


def form_id(local_id, master_index=SELF_INDEX):
    assert 0 <= local_id <= 0xFFFFFF
    return (master_index << 24) | local_id


def zstring(text):
    return text.encode("cp1252") + b"\x00"


def wstring(text):
    """String do VMAD: uint16 tamanho + bytes, sem terminador."""
    data = text.encode("cp1252")
    return struct.pack("<H", len(data)) + data


def subrecord(sig, data):
    assert len(sig) == 4 and len(data) <= 0xFFFF, sig
    return sig.encode("ascii") + struct.pack("<H", len(data)) + data


def record(sig, flags, fid, payload):
    # type, dataSize, flags, formID, timestamp, version control, internal version, unknown
    return sig.encode("ascii") + struct.pack("<IIIHHHH", len(payload), flags, fid, 0, 0, FORM_VERSION, 0) + payload


def group(label, records):
    payload = b"".join(records)
    # "GRUP", tamanho TOTAL (com os 24 bytes do cabeçalho), label, tipo 0 (top), timestamp, vc, unknown
    return b"GRUP" + struct.pack("<I4sIHHI", 24 + len(payload), label.encode("ascii"), 0, 0, 0, 0) + payload


def vmad_script(name, flags=0, properties=b"", property_count=0):
    return wstring(name) + struct.pack("<BH", flags, property_count) + properties


def vmad_object_v2(fid, alias_id=-1):
    # Object format 2: uint16 unused, int16 alias, uint32 formID
    return struct.pack("<HhI", 0, alias_id, fid)


def mcm_quest():
    fid = form_id(MCM_QUEST_LOCAL_ID)

    vmad = struct.pack("<hhH", 5, 2, 1)                      # version 5, object format 2, 1 script
    vmad += vmad_script(MCM_SCRIPT)
    vmad += struct.pack("<bH", 2, 0)                         # fragments: extra bind data v2, 0 fragments
    vmad += wstring("")                                      # fileName (sempre presente)
    vmad += struct.pack("<H", 1)                             # 1 alias
    vmad += vmad_object_v2(fid, PLAYER_ALIAS_ID)             # alias 0 desta quest
    vmad += struct.pack("<hhH", 5, 2, 1)                     # version, object format, 1 script
    vmad += vmad_script(PLAYER_ALIAS_SCRIPT)

    # DNAM: flags u16, priority u8, form version u8, unknown 4 bytes, type u32
    dnam = struct.pack("<HBB4sI", QUST_FLAG_START_GAME_ENABLED, 0, 0, b"\x00" * 4, 0)

    subs = [
        subrecord("EDID", zstring("AoS_MCMQuest")),
        subrecord("VMAD", vmad),
        subrecord("DNAM", dnam),
        subrecord("NEXT", b""),
        subrecord("ANAM", struct.pack("<I", PLAYER_ALIAS_ID + 1)),  # próximo alias id
        subrecord("ALST", struct.pack("<I", PLAYER_ALIAS_ID)),
        subrecord("ALID", zstring(PLAYER_ALIAS_NAME)),
        subrecord("FNAM", struct.pack("<I", 0)),
        subrecord("ALFR", struct.pack("<I", PLAYER_REF)),
        subrecord("ALED", b""),
    ]
    return record("QUST", 0, fid, b"".join(subs))


# Grupos de topo, na ordem em que aparecem no arquivo.
RECORDS = {
    "QUST": [mcm_quest],
}


def build():
    groups = []
    count = 0
    for label, makers in RECORDS.items():
        recs = [m() for m in makers]
        groups.append(group(label, recs))
        count += 1 + len(recs)  # HEDR conta records + grupos

    hedr = struct.pack("<fiI", HEDR_VERSION, count, NEXT_OBJECT_ID)
    tes4 = [subrecord("HEDR", hedr), subrecord("CNAM", zstring(AUTHOR)), subrecord("SNAM", zstring(DESCRIPTION))]
    for m in MASTERS:
        tes4 += [subrecord("MAST", zstring(m)), subrecord("DATA", struct.pack("<Q", 0))]
    header = record("TES4", TES4_FLAG_ESL, 0, b"".join(tes4))
    return header + b"".join(groups)


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    data = build()
    with open(sys.argv[1], "wb") as f:
        f.write(data)
    print(f"{sys.argv[1]}: {len(data)} bytes.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
