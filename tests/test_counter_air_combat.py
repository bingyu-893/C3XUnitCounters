"""Compile and run production counter code with controlled engine dependencies.

Optional native-loop checks use locally owned, unmodified executable code only:
  python tests/test_counter_air_combat.py --steam-exe PATH --gog-text PATH --pcg-text PATH
The .text dumps start at VA 0x401000 (see Notes/air_combat_counters.md).
No game binaries are distributed by this test.
"""
import argparse
import csv
import hashlib
from pathlib import Path
import re
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def function(source, name):
    match = re.search(r"^([^\n;{}]+\n" + name + r" \([^;]*?\n\{\n.*?^\})", source, re.M | re.S)
    assert match, name
    return match[1] + "\n"


def native_fixtures(args):
    # Addresses verified independently against each program's machine code.
    versions = [
        ("gog", args.gog_text, 0x4A4520, 0x4A4555, 0x9FD4BC,
         [0x5BE6E0, 0x5BE820, 0x4A0ED0, 0x60BAB0, 0x5BE5B0, 0x5BE5B0,
          0x47B530, 0x47B550, 0x473F80, 0x4ED120, 0x5C9F10, 0x4A46A0]),
        ("steam", args.steam_exe, 0x4AB1C0, 0x4AB1F5, 0xA1FCBC,
         [0x5CD2C0, 0x5CD420, 0x4A7A90, 0x626440, 0x5CD180, 0x5CD180,
          0x47E9B0, 0x47E9D0, 0x476DB0, 0x4F6040, 0x5D8E50, 0x4AB340]),
        ("pcg", args.pcg_text, 0x4A45B0, 0x4A45E5, 0x9FD47C,
         [0x5BE3F0, 0x5BE530, 0x4A0F60, 0x60B9E0, 0x5BE2C0, 0x5BE2C0,
          0x47B5B0, 0x47B5D0, 0x474000, 0x4ED1E0, 0x5C9C20, 0x4A4730]),
    ]
    fixtures = []
    rows = list(csv.reader((ROOT / "civ_prog_objects.csv").read_text().replace("\t", " ").splitlines(), skipinitialspace=True))
    symbols = {r[4].strip().strip('"'): r for r in rows[1:] if len(r) == 6}
    for column, (name, path, entry, odds_call, player, targets) in enumerate(versions, 1):
        assert int(symbols["Fighter_do_air_combat"][column], 0) == entry
        assert int(symbols["Fighter_get_odds_for_air_combat"][column], 0) == odds_call
        if not path:
            continue
        import capstone
        data = path.read_bytes()
        if name == "steam":
            pe = struct.unpack_from("<I", data, 0x3C)[0]
            count = struct.unpack_from("<H", data, pe + 6)[0]
            opt_size = struct.unpack_from("<H", data, pe + 20)[0]
            for n in range(count):
                off = pe + 24 + opt_size + 40 * n
                if data[off:off + 8].rstrip(b"\0") == b".text":
                    _, va, size, raw = struct.unpack_from("<IIII", data, off + 8)
                    assert va == 0x1000
                    data = data[raw:raw + size]
                    break
            else:
                raise AssertionError("Missing .text")
        code = data[entry - 0x401000:entry - 0x401000 + 0x179]
        expected_hash = {
            "gog": "6b714cd06b152a3d21d1a85106d45b98f65c3ed786dc90727c2e946ecbe0dad8",
            "steam": "db8bcefa779c1bbd94828719dc408f086dd8b454a8ad9d67d887179a10a9cd1a",
            "pcg": "f6d0ac4f22c040bf0b11d848c2b875586bfb72db57bfe892b2eb4ce68370ea7a",
        }[name]
        assert hashlib.sha256(code).hexdigest() == expected_hash, "Unverified native loop"
        offline_call, online_call, intercept_roll = {
            "gog": (0x5C719E, 0x478A35, 0x5C70C4),
            "steam": (0x5D6040, 0x47BA25, 0x5D5F72),
            "pcg": (0x5C6EAE, 0x478AB5, 0x5C6DD4),
        }[name]
        for site, destination in [(offline_call, entry), (online_call, entry), (intercept_roll, targets[3])]:
            offset = site - 0x401000
            assert data[offset] == 0xE8
            assert site + 5 + struct.unpack_from("<i", data, offset + 1)[0] == destination
        offset = offline_call - 0x401000
        assert data[offset - 9:offset - 5] == bytes.fromhex("51 52 55 56")  # y, x, mission, interceptor
        md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        md.detail = True
        instructions = list(md.disasm(code, entry))
        calls = [i for i in instructions if i.mnemonic == "call"]
        assert [i.operands[0].imm for i in calls] == targets, name
        assert calls[2].address == odds_call
        assert code[:8] == bytes.fromhex("51 53 55 56 8b 74 24 14")
        assert code[-3:] == bytes.fromhex("c2 10 00")
        assert calls[2].address + 0x11 == calls[3].address  # untouched RNG call
        player_refs = [i for i in instructions if i.mnemonic == "mov" and
                       i.op_str == f"ecx, dword ptr [0x{player:x}]"]
        assert len(player_refs) == 1
        print(f"{name}: entry/odds/RNG/ABI verified; air function SHA256 {hashlib.sha256(code).hexdigest()}", flush=True)
        fixtures.append((name, code, [i.address - entry + 1 for i in calls],
                         player_refs[0].address - entry + 2))
    return fixtures


def main():
    parser = argparse.ArgumentParser(__doc__)
    for option in ("steam-exe", "gog-text", "pcg-text"):
        parser.add_argument("--" + option, type=Path)
    args = parser.parse_args()
    fixtures = native_fixtures(args)
    source = (ROOT / "injected_code.c").read_text(encoding="utf-8")
    names = ["free_counter_rule_match_cache", "unit_matches_counter_side",
             "counter_rule_experience_mask_matches", "counter_rule_experience_conditions_match"]
    generated = "\n".join(function(source, n) for n in names)
    generated += source[source.index("struct counter_tile_env {"):source.index("void\nformat_counter_percent (")]
    for name in ["format_counter_percent", "init_neutral_counter_effect_summary", "get_counter_effect_summary",
                 "unit_has_valid_type_id", "format_counter_air_combat_debug",
                 "patch_Fighter_do_air_combat", "patch_Fighter_get_odds_for_air_combat"]:
        generated += function(source, name)
    fixture_header = f"#define NATIVE_COUNT {len(fixtures)}\n"
    for n, (_, code, _, _) in enumerate(fixtures):
        fixture_header += f"static unsigned char native_code_{n}[] = {{" + ",".join(map(str, code)) + "};\n"
    fixture_header += "static struct native_fixture fixtures[] = {\n"
    for n, (name, code, calls, player) in enumerate(fixtures):
        fixture_header += f'{{"{name}",native_code_{n},{len(code)},{{' + ",".join(map(str, calls)) + f"}},{player}}},\n"
    fixture_header += "{0}};\n"
    scratch = (ROOT / "ref").resolve()
    scratch.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="c3x-air-", dir=scratch) as temp:
        temp = Path(temp).resolve()
        assert temp.parent == scratch  # Keep automatic cleanup inside the workspace.
        (temp / "counter_air_generated.h").write_text(generated, encoding="utf-8")
        (temp / "counter_air_native.h").write_text(fixture_header, encoding="utf-8")
        subprocess.run([str(ROOT / "tcc/tcc.exe"), "-m32", "-I" + str(ROOT), "-I" + str(temp),
                        "-lkernel32", "-run", str(ROOT / "tests/counter_air_combat.c")], check=True)


if __name__ == "__main__":
    main()
