#!/usr/bin/env python3
"""PE forensics for QueensDomain-Win64-Shipping.exe (read-only)."""
import hashlib
import struct
import sys
import pefile

PATH = r"G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Binaries\Win64\QueensDomain-Win64-Shipping.exe"


def entropy(b):
    if not b:
        return 0.0
    from collections import Counter
    import math
    c = Counter(b)
    n = len(b)
    return -sum(v / n * math.log2(v / n) for v in c.values())


def main():
    raw = open(PATH, "rb").read()
    print("=" * 78)
    print("FILE")
    print("=" * 78)
    print("path       :", PATH)
    print("size       : %d (%.2f MB)" % (len(raw), len(raw) / 1048576))
    print("sha256     :", hashlib.sha256(raw).hexdigest())
    print("md5        :", hashlib.md5(raw).hexdigest())
    print("first bytes:", raw[:2].hex())

    pe = pefile.PE(PATH, fast_load=False)
    print()
    print("=" * 78)
    print("HEADERS")
    print("=" * 78)
    print("machine    : 0x%04X %s" % (
        pe.FILE_HEADER.Machine,
        {0x8664: "AMD64/x86-64", 0x14c: "i386"}.get(pe.FILE_HEADER.Machine, "?")))
    print("num sections: %d" % pe.FILE_HEADER.NumberOfSections)
    td = pe.FILE_HEADER.TimeDateStamp
    import datetime
    print("timestamp  : 0x%08X = %s UTC" % (
        td, datetime.datetime.utcfromtimestamp(td).strftime("%Y-%m-%d %H:%M:%S")))
    print("characteristics: 0x%04X" % pe.FILE_HEADER.Characteristics)
    chars = pe.FILE_HEADER.Characteristics
    for bit, name in [(0x0002, "EXECUTABLE_IMAGE"), (0x0020, "LARGE_ADDRESS_AWARE"),
                      (0x0001, "RELOCS_STRIPPED"), (0x0100, "32BIT_MACHINE"),
                      (0x2000, "DLL")]:
        print("    %-24s %s" % (name, bool(chars & bit)))
    opt = pe.OPTIONAL_HEADER
    print("magic      : 0x%X (%s)" % (opt.Magic, "PE32+" if opt.Magic == 0x20b else "PE32"))
    print("entry RVA  : 0x%X" % opt.AddressOfEntryPoint)
    print("image base : 0x%X" % opt.ImageBase)
    print("size image : 0x%X" % opt.SizeOfImage)
    print("subsystem  : %d (%s)" % (opt.Subsystem, pefile.SUBSYSTEM_TYPE.get(opt.Subsystem, "?")))
    print("dll chars  : 0x%04X" % opt.DllCharacteristics)
    for bit, name in [(0x0020, "HIGH_ENTROPY_VA"), (0x0040, "DYNAMIC_BASE"),
                      (0x0080, "FORCE_INTEGRITY"), (0x0100, "NX_COMPAT"),
                      (0x0200, "NO_ISOLATION"), (0x0400, "NO_SEH"),
                      (0x0800, "NO_BIND"), (0x1000, "APPCONTAINER"),
                      (0x2000, "WDM_DRIVER"), (0x4000, "GUARD_CF"),
                      (0x8000, "TERMINAL_SERVER_AWARE")]:
        print("    %-24s %s" % (name, bool(opt.DllCharacteristics & bit)))
    print("checksum   : 0x%X (stored) " % opt.CheckSum)

    print()
    print("=" * 78)
    print("SECTIONS")
    print("=" * 78)
    print("%-10s %10s %10s %10s %10s %8s  %s" % (
        "name", "vaddr", "vsize", "rawptr", "rawsize", "entropy", "chars"))
    for s in pe.sections:
        nm = s.Name.rstrip(b"\0").decode("latin1")
        body = s.get_data() if s.SizeOfRawData else b""
        e = entropy(body)
        print("%-10s %10X %10X %10X %10X %8.4f  %s" % (
            nm, s.VirtualAddress, s.Misc_VirtualSize, s.PointerToRawData,
            s.SizeOfRawData, e, hex(s.Characteristics)))

    # Rich header
    print()
    print("=" * 78)
    print("RICH HEADER / LINKER")
    print("=" * 78)
    rich = parse_rich(raw)
    if rich:
        for k, v in rich.items():
            print("%-14s: %s" % (k, v))
    else:
        print("no Rich header found")

    print()
    print("=" * 78)
    print("IMPORTS (DLLs)")
    print("=" * 78)
    if hasattr(pe, "DIRECTORY_ENTRY_IMPORT"):
        for e in pe.DIRECTORY_ENTRY_IMPORT:
            dll = e.dll.decode("latin1")
            n = len(e.imports)
            print("%-40s %4d imports" % (dll, n))
    else:
        print("no import directory")

    print()
    print("=" * 78)
    print("DELAY IMPORTS")
    print("=" * 78)
    if hasattr(pe, "DIRECTORY_ENTRY_DELAY_IMPORT"):
        for e in pe.DIRECTORY_ENTRY_DELAY_IMPORT:
            print("%-40s %4d" % (e.dll.decode("latin1"), len(e.imports)))
    else:
        print("none")

    print()
    print("=" * 78)
    print("EXPORTS")
    print("=" * 78)
    if hasattr(pe, "DIRECTORY_ENTRY_EXPORT"):
        ex = pe.DIRECTORY_ENTRY_EXPORT
        print("module:", ex.name)
        for s in ex.symbols:
            print("  ord %-5d RVA %08X  %s" % (s.ordinal, s.address, s.name))
    else:
        print("no export directory")

    print()
    print("=" * 78)
    print("DATA DIRECTORIES")
    print("=" * 78)
    for i, d in enumerate(pe.OPTIONAL_HEADER.DATA_DIRECTORY):
        if d.VirtualAddress or d.Size:
            print("%-28s RVA %08X  size %08X" % (d.name, d.VirtualAddress, d.Size))

    print()
    print("=" * 78)
    print("DEBUG DIRECTORY")
    print("=" * 78)
    if hasattr(pe, "DIRECTORY_ENTRY_DEBUG"):
        for d in pe.DIRECTORY_ENTRY_DEBUG:
            print("type=%d size=%d" % (d.struct.Type, d.struct.SizeOfData))
            print("  raw dump:", d.entry.hex() if hasattr(d, "entry") else "n/a")
    else:
        print("none")


def parse_rich(raw):
    out = {}
    idx = raw.find(b"Rich")
    if idx < 0:
        return None
    # find 'DanS' marker before it
    dans = raw.rfind(b"DanS", 0, idx)
    if dans < 0:
        return None
    key = struct.unpack_from("<I", raw, idx + 4)[0]
    out["rich_offset"] = "0x%X" % idx
    out["dans_offset"] = "0x%X" % dans
    out["xor_key"] = "0x%08X" % key
    # decrypt from dans+4 to idx
    ents = []
    p = dans + 4
    while p + 8 <= idx:
        cv, cnt = struct.unpack_from("<II", raw, p)
        cv ^= key
        cnt ^= key
        ents.append((cv, cnt))
        p += 8
    out["entries"] = len(ents)
    # comp id: high 16 bits = build tool id, low 16 bits = build number
    prodid_names = {}
    for cv, cnt in ents:
        prodid = (cv >> 16) & 0xFFFF
        build = cv & 0xFFFF
        prodid_names.setdefault(prodid, []).append((build, cnt))
    for prodid in sorted(prodid_names):
        for build, cnt in sorted(prodid_names[prodid]):
            out.setdefault("products", []).append(
                "prodid=%d(%s) build=%d count=%d" % (prodid, prodname(prodid), build, cnt))
    return out


def prodname(pid):
    return {0: "Unknown", 1: "Import0", 2: "Linker510", 3: "Cvtomf510",
            4: "Linker600", 5: "Cvtomf600", 6: "Cvtres500", 7: "Utc11_Basic",
            8: "Utc11_C", 9: "Utc12_Basic", 10: "Utc12_C", 11: "Utc12_CPP",
            12: "AliasObj60", 13: "VisualBasic60", 14: "Masm613",
            15: "Masm710", 16: "Linker511", 17: "Cvtomf511", 18: "Masm614",
            19: "Linker512", 20: "Cvtomf512", 21: "Utc12_C_Std", 22: "Utc12_CPP_Std",
            23: "Utc12_C_Book", 24: "Utc12_CPP_Book", 25: "Implib700",
            26: "Cvtomf700", 27: "Utc13_Basic", 28: "Utc13_C", 29: "Utc13_CPP",
            30: "Linker610", 31: "Cvtomf610", 32: "Linker601", 33: "Cvtomf601",
            34: "Utc12_1_Basic", 35: "Utc12_1_C", 36: "Utc12_1_CPP",
            37: "Linker620", 38: "Cvtomf620", 39: "AliasObj70",
            40: "Linker621", 41: "Cvtomf621", 42: "Masm615", 43: "Utc13_LTCG_C",
            44: "Utc13_LTCG_CPP", 45: "Masm620", 46: "ILAsm100",
            47: "Utc12_2_Basic", 48: "Utc12_2_C", 49: "Utc12_2_CPP",
            50: "Utc12_2_C_Std", 51: "Utc12_2_CPP_Std", 52: "Utc12_2_C_Book",
            53: "Utc12_2_CPP_Book", 54: "Implib622", 55: "Cvtomf622",
            56: "Cvtres501", 57: "Utc13_C_Std", 58: "Utc13_CPP_Std",
            59: "Cvtpgd1300", 60: "Linker622", 61: "Linker700",
            62: "Export622", 63: "Export700", 64: "Masm700", 65: "Utc13_POGO_I_C",
            66: "Utc13_POGO_I_CPP", 67: "Utc13_POGO_O_C", 68: "Utc13_POGO_O_CPP",
            69: "Cvtres700", 70: "Cvtres710p", 71: "Linker710p", 72: "Cvtomf710p",
            73: "Export710p", 74: "Implib710p", 75: "Masm710p",
            76: "Utc1310p_C", 77: "Utc1310p_CPP", 78: "Utc1310p_C_Std",
            79: "Utc1310p_CPP_Std", 80: "Utc1310p_LTCG_C", 81: "Utc1310p_LTCG_CPP",
            82: "Utc1310p_POGO_I_C", 83: "Utc1310p_POGO_I_CPP",
            84: "Utc1310p_POGO_O_C", 85: "Utc1310p_POGO_O_CPP", 86: "Linker624",
            87: "Cvtomf624", 88: "Export624", 89: "Implib624", 90: "Linker710",
            91: "Cvtomf710", 92: "Export710", 93: "Implib710", 94: "Cvtres710",
            95: "Utc1310_C", 96: "Utc1310_CPP", 97: "Utc1310_C_Std",
            98: "Utc1310_CPP_Std", 99: "Utc1310_LTCG_C", 100: "Utc1310_LTCG_CPP",
            101: "Utc1310_POGO_I_C", 102: "Utc1310_POGO_I_CPP",
            103: "Utc1310_POGO_O_C", 104: "Utc1310_POGO_O_CPP",
            105: "AliasObj710", 106: "AliasObj710p", 107: "Cvtpgd1310",
            108: "Cvtpgd1310p", 109: "Utc1400_C", 110: "Utc1400_CPP",
            111: "Utc1400_C_Std", 112: "Utc1400_CPP_Std", 113: "Utc1400_LTCG_C",
            114: "Utc1400_LTCG_CPP", 115: "Utc1400_POGO_I_C",
            116: "Utc1400_POGO_I_CPP", 117: "Utc1400_POGO_O_C",
            118: "Utc1400_POGO_O_CPP", 119: "Cvtpgd1400", 120: "Linker800",
            121: "Cvtomf800", 122: "Export800", 123: "Implib800",
            124: "Cvtres800", 125: "Masm800", 126: "AliasObj800",
            127: "PhoenixPrerelease", 128: "Utc1400_CVTCIL_C",
            129: "Utc1400_CVTCIL_CPP", 130: "Utc1400_LTCG_MSIL",
            131: "Utc1500_C", 132: "Utc1500_CPP", 133: "Utc1500_C_Std",
            134: "Utc1500_CPP_Std", 135: "Utc1500_CVTCIL_C",
            136: "Utc1500_CVTCIL_CPP", 137: "Utc1500_LTCG_C",
            138: "Utc1500_LTCG_CPP", 139: "Utc1500_LTCG_MSIL",
            140: "Utc1500_POGO_I_C", 141: "Utc1500_POGO_I_CPP",
            142: "Utc1500_POGO_O_C", 143: "Utc1500_POGO_O_CPP",
            144: "Cvtpgd1500", 145: "Linker900", 146: "Export900",
            147: "Implib900", 148: "Cvtres900", 149: "Masm900",
            150: "AliasObj900", 151: "Resource900", 152: "AliasObj1000",
            154: "Cvtres1000", 155: "Export1000", 156: "Implib1000",
            157: "Linker1000", 158: "Masm1000", 170: "Utc1600_C",
            171: "Utc1600_CPP", 172: "Utc1600_CVTCIL_C", 173: "Utc1600_CVTCIL_CPP",
            174: "Utc1600_LTCG_C", 175: "Utc1600_LTCG_CPP", 176: "Utc1600_LTCG_MSIL",
            177: "Utc1600_POGO_I_C", 178: "Utc1600_POGO_I_CPP",
            179: "Utc1600_POGO_O_C", 180: "Utc1600_POGO_O_CPP",
            181: "Cvtpgd1600", 182: "Linker1010", 183: "Export1010",
            184: "Implib1010", 185: "Cvtres1010", 186: "Masm1010",
            187: "AliasObj1010", 188: "Utc1700_C", 189: "Utc1700_CPP",
            190: "Utc1700_CVTCIL_C", 191: "Utc1700_CVTCIL_CPP",
            192: "Utc1700_LTCG_C", 193: "Utc1700_LTCG_CPP", 194: "Utc1700_LTCG_MSIL",
            195: "Utc1700_POGO_I_C", 196: "Utc1700_POGO_I_CPP",
            197: "Utc1700_POGO_O_C", 198: "Utc1700_POGO_O_CPP",
            199: "Cvtpgd1700", 200: "Linker1100", 201: "Export1100",
            202: "Implib1100", 203: "Cvtres1100", 204: "Masm1100",
            205: "AliasObj1100", 206: "Utc1800_C", 207: "Utc1800_CPP",
            208: "Utc1800_CVTCIL_C", 209: "Utc1800_CVTCIL_CPP",
            210: "Utc1800_LTCG_C", 211: "Utc1800_LTCG_CPP", 212: "Utc1800_LTCG_MSIL",
            213: "Utc1800_POGO_I_C", 214: "Utc1800_POGO_I_CPP",
            215: "Utc1800_POGO_O_C", 216: "Utc1800_POGO_O_CPP",
            217: "Utc1800_POGO_I_MSIL", 218: "Utc1800_POGO_O_MSIL",
            219: "Cvtpgd1800", 220: "Linker1110", 221: "Export1110",
            222: "Implib1110", 223: "Cvtres1110", 224: "Masm1110",
            225: "AliasObj1110", 226: "Utc1900_C", 227: "Utc1900_CPP",
            228: "Utc1900_CVTCIL_C", 229: "Utc1900_CVTCIL_CPP",
            230: "Utc1900_LTCG_C", 231: "Utc1900_LTCG_CPP", 232: "Utc1900_LTCG_MSIL",
            233: "Utc1900_POGO_I_C", 234: "Utc1900_POGO_I_CPP",
            235: "Utc1900_POGO_O_C", 236: "Utc1900_POGO_O_CPP",
            237: "Utc1900_POGO_I_MSIL", 238: "Utc1900_POGO_O_MSIL",
            239: "Cvtpgd1900", 240: "Linker1120", 241: "Export1120",
            242: "Implib1120", 243: "Cvtres1120", 244: "Masm1120",
            245: "AliasObj1120", 246: "Utc1910_C", 247: "Utc1910_CPP",
            248: "Utc1910_CVTCIL_C", 249: "Utc1910_CVTCIL_CPP",
            250: "Utc1910_LTCG_C", 251: "Utc1910_LTCG_CPP", 252: "Utc1910_LTCG_MSIL",
            253: "Utc1910_POGO_I_C", 254: "Utc1910_POGO_I_CPP",
            255: "Utc1910_POGO_O_C", 256: "Utc1910_POGO_O_CPP",
            257: "Utc1910_POGO_I_MSIL", 258: "Utc1910_POGO_O_MSIL",
            259: "Cvtpgd1910", 260: "Linker1130", 261: "Export1130",
            262: "Implib1130", 263: "Cvtres1130", 264: "Masm1130",
            265: "AliasObj1130", 266: "Utc1920_C", 267: "Utc1920_CPP",
            268: "Utc1920_CVTCIL_C", 269: "Utc1920_CVTCIL_CPP",
            270: "Utc1920_LTCG_C", 271: "Utc1920_LTCG_CPP", 272: "Utc1920_LTCG_MSIL",
            273: "Utc1920_POGO_I_C", 274: "Utc1920_POGO_I_CPP",
            275: "Utc1920_POGO_O_C", 276: "Utc1920_POGO_O_CPP",
            277: "Utc1920_POGO_I_MSIL", 278: "Utc1920_POGO_O_MSIL",
            279: "Cvtpgd1920", 280: "Linker1140", 281: "Export1140",
            282: "Implib1140", 283: "Cvtres1140", 284: "Masm1140",
            285: "AliasObj1140", 293: "Linker1000_v", }.get(pid, "?%d" % pid)


if __name__ == "__main__":
    main()
