"""TimeSplitters 2 (PAL): back to 60 ticks a second.

The game's clock is the vertical blank: a frame's ticks are the blanks
since the last (sub_001AE3C0). The USA release turns seconds into ticks and
back with 60 and 1/60; the PAL release, made for a 50 Hz console, has 50 and
1/50 in the same places, so that a second still lasted a second at 50
blanks a second. The host's blank is 60 Hz whatever the title asks for, so
on the PAL release here everything given in seconds ran 1.2 times too fast:
the intro cutscene's pictures ended at 120 s against its 143.6 s soundtrack
(music/cs_intro_full44july26fx.wav), the dialogue falling a second behind
every five, and timers and speeds given per second ran fast with them.

The compiler pooled the constants: 1/50 is one float at 0x00220350 and 50.0
one at 0x00220644, each also read by code where the USA release has 1/50 or
50 too (a genuine 0.02 or 50). So the constants stay, and each instruction
that reads them where the USA release reads 1/60 or 60 is pointed at the
PAL release's own copy of that (1/60 at 0x0021C084, 60.0 at 0x00220388,
both read-only and already read elsewhere): an operand change, the
instruction keeps its length.

The sites were found by listing every use of 50, 1/50, 60 and 1/60 (float
and double, loaded or immediate) in both releases with Ghidra, pairing the
PAL and USA instruction by function and offset (the USA code is the PAL
code moved by 0 to 0x450), then by the bytes around each one with
addresses masked. PER_TICK_SITES_BY_REGION are the uses of 1/50 in
0x00170000-0x0018B000 that neither pairing reached; every use of 1/50
there that one did reach had 1/60 in the USA release. Left as they are,
because the USA release has 50 or 0.02 there too, or because it could not
be told: 0x001841AB and 0x00176EA5 (50.0), 0x00138E9F and 0x00094D9B
(1/50), and 18 immediates of 50.0 or 0.02 in 0x00163821-0x00189147.

Integer counts of ticks (a wait of 50 for a second) are not covered.
"""

import struct
import sys

from capstone import x86

INV50, INV60 = 0x00220350, 0x0021C084        # 1/50 and 1/60, floats
F50, F60 = 0x00220644, 0x00220388            # 50.0 and 60.0
IMM_INV50, IMM_INV60 = 0x3CA3D70A, 0x3C888889

PER_TICK_SITES = [
    0x00015E43, 0x000168EB, 0x0001694E, 0x00023474, 0x000274C4, 0x00027568, 0x00027599,
    0x00028958, 0x0002B1E0, 0x0003103D, 0x000311F8, 0x000312F4, 0x00032A40, 0x00032F56,
    0x00034A0A, 0x00035416, 0x00037ACF, 0x00037EFA, 0x0003884A, 0x0003AAD3, 0x0003BB7E,
    0x0003BECA, 0x0003C3D2, 0x0003E7EA, 0x0003EA7F, 0x000451CC, 0x00045741, 0x00045B7B,
    0x00045D1A, 0x00045DC7, 0x00045F08, 0x00046027, 0x00046333, 0x00046688, 0x00046A9D,
    0x00047048, 0x0004752C, 0x0004755A, 0x000475B0, 0x00048269, 0x00049704, 0x0004A036,
    0x0004A30F, 0x0005175F, 0x000517C7, 0x00051919, 0x0005B66E, 0x0005B68C, 0x0005F1D8,
    0x00065719, 0x00065D85, 0x00065D9D, 0x0006925C, 0x000692F4, 0x00069312, 0x0006DF49,
    0x0006E472, 0x00072DC9, 0x00073A5D, 0x00078167, 0x000785D9, 0x0007BB98, 0x0007E8D0,
    0x0007E91C, 0x00082B98, 0x00084FDB, 0x000881C9, 0x0008911E, 0x00094918, 0x0009514F,
    0x0009518A, 0x00099530, 0x0009F1CE, 0x0009F426, 0x0009FBB9, 0x000A23CA, 0x000A42D8,
    0x000A4973, 0x000A5D76, 0x000A7E36, 0x000A8315, 0x000A8CDD, 0x000A92E7, 0x000ABBC2,
    0x000ACBEE, 0x000ACE36, 0x000ADCD4, 0x000B1B25, 0x000B3E64, 0x000B5150, 0x000B523B,
    0x000B553A, 0x000B57E2, 0x000B5BCB, 0x000B650E, 0x000B7239, 0x000B7670, 0x000B7B1A,
    0x000B7E80, 0x000BC3F1, 0x000BD328, 0x000BDFBD, 0x000C4DD7, 0x000C6FA8, 0x000C7AC4,
    0x000CA95C, 0x000CB226, 0x000D8A5C, 0x000D938F, 0x000DA0BD, 0x000DB977, 0x000DB9B5,
    0x000DBA08, 0x000DBA3A, 0x000DBA6C, 0x000DBA9E, 0x000DBAD0, 0x000DBB02, 0x000E413C,
    0x000E45D8, 0x000E564E, 0x000E7AC3, 0x000EB7E5, 0x000EE3CA, 0x000EEB99, 0x000EFA64,
    0x000F07B7, 0x000F080B, 0x000F1D3A, 0x000F2416, 0x000F2A66, 0x000F5A66, 0x00110FB6,
    0x00120FEF, 0x001321E4, 0x001334E0, 0x00133C42, 0x001344F6, 0x00134923, 0x001378A8,
    0x00137992, 0x00137EA8, 0x00137FDD, 0x0013822D, 0x0013843E, 0x0013878A, 0x00138A0B,
    0x00138A1A, 0x00138A29, 0x00138A5A, 0x0013CD10, 0x0013CF80, 0x0013D779, 0x0013FCF3,
    0x0014123F, 0x00142140, 0x00142508, 0x00142557, 0x001425A8, 0x00142769, 0x0014440A,
    0x00145360, 0x00145477, 0x001454B6, 0x00145AD9, 0x00145AFB, 0x00145FC3, 0x001465ED,
    0x00146831, 0x001468F7, 0x00146A66, 0x00146C76, 0x00146D1D, 0x00146F31, 0x0014734D,
    0x00147381, 0x00148745, 0x0014875D, 0x0014877B, 0x001492DE, 0x0014930C, 0x001494B7,
    0x0014C6C8, 0x0014D233, 0x0014D268, 0x0014D292, 0x0014D2B9, 0x0014D2E4, 0x0014D662,
    0x0015114D, 0x0015364D, 0x00155DDC, 0x001561CF, 0x00157E94, 0x00157EA3, 0x00157EB2,
    0x001580EB, 0x001584E5, 0x0015895B, 0x00158D94, 0x00159A95, 0x00159BF1, 0x0015A76D,
    0x0015B788, 0x0015BA4F, 0x0015BAA4, 0x0015D8F5, 0x001607F0, 0x0016232B, 0x001658BD,
    0x001673CA, 0x00167B48, 0x0016888C, 0x00168CF6, 0x00168D43, 0x00169FB9, 0x00169FE2,
    0x0016A068, 0x0016A0A0, 0x0016A0C9, 0x0016A135, 0x0016A15E, 0x0016A1E4, 0x0016A210,
    0x0016A27C, 0x0016A2A5, 0x0016A309, 0x0016BF0C, 0x0016BF18, 0x0016E66A, 0x0016EAFB,
    0x0017114B, 0x0017182D, 0x00171868, 0x00171970, 0x00171A8E, 0x00171DEB, 0x00172092,
    0x00172133, 0x0017352E, 0x00176268, 0x00176807, 0x0017697B, 0x00177A96, 0x00177AF5,
    0x00177F39, 0x001783EB, 0x00179DBE, 0x00179EE0, 0x0017A1CE, 0x0017BA7A, 0x0017C5B7,
    0x0017E0E0, 0x0017E1DA, 0x0017E443, 0x0017E497, 0x0017EB9C, 0x0017EDAE, 0x0018034A,
    0x00180539, 0x00180AB3, 0x00181230, 0x00181273, 0x00181AED, 0x0018406A, 0x00184C27,
    0x00185419, 0x00185A6B, 0x001872E9, 0x00187E9A, 0x001881AD, 0x001881BF, 0x001881D1,
    0x001886BD, 0x00188D2E, 0x00189AD9, 0x0018BC47, 0x0018C03F, 0x0018C7A3, 0x0018C7BD,
    0x0018E12D, 0x0018E216, 0x0018E49E, 0x0018ED2D, 0x0018F021, 0x00190CB7, 0x00191067,
    0x0019108E, 0x00191120, 0x00191147, 0x00191E70, 0x0019233E, 0x00196DC6, 0x0019713F,
    0x00197EAA, 0x001990AA, 0x001993DC, 0x0019B08B, 0x0019B1F9, 0x0019EF94, 0x001A0821,
    0x001A2DBB, 0x001A3DC9, 0x001A4062, 0x001A4500, 0x001A4A8A, 0x001A507A, 0x001A5DB6,
    0x001A71A9, 0x001A7809, 0x001A82D8, 0x001AA09B, 0x001AF746, 0x001B132F, 0x001B2331,
    0x001B29FC, 0x001B45E9,
]

PER_TICK_SITES_BY_REGION = [
    0x00170E5F, 0x00177444, 0x0017E2EF, 0x0017F217, 0x0017F28F, 0x00180153, 0x00180DE4,
    0x00185289, 0x0018822F, 0x001882B6, 0x00188DC5, 0x0018993E, 0x0018A7EC,
]

PER_SECOND_SITES = [
    0x000328DA, 0x00032A81, 0x00032A97, 0x00032AAD, 0x00032AD8, 0x0003C5F3, 0x00045239,
    0x00045252, 0x00045262, 0x0005176B, 0x000541A0, 0x0005B971, 0x0005BA90, 0x0005BAE2,
    0x0005BBF1, 0x0005FEB2, 0x0005FECD, 0x000635CD, 0x000635F2, 0x00078AED, 0x00078BD2,
    0x00078CCE, 0x00078D47, 0x0007B9EE, 0x0007C53F, 0x0007C572, 0x00083D27, 0x00083DE8,
    0x000B509F, 0x000BB988, 0x000C4D59, 0x00148E5D, 0x001AE628, 0x001AE645, 0x001BEB35,
]

PER_TICK_IMM_SITES = [
    0x000B3D55,
]


def operations(img, insn_at):
    """The patch operations, each checked against the image: the
    instruction at every site must read the PAL constant through its
    displacement (or carry it as its immediate)."""
    consts = {INV50: 0.02, INV60: 1.0 / 60, F50: 50.0, F60: 60.0}
    for va, want in consts.items():
        have = struct.unpack("<f", img.read(va, 4, "constant"))[0]
        if abs(have - want) > 1e-6 * want:
            sys.exit("0x%08X holds %g, not %g" % (va, have, want))
    ops = []

    def repoint(va, old, new, what):
        i = insn_at(va)
        disps = [o.mem.disp & 0xFFFFFFFF for o in i.operands if o.type == x86.X86_OP_MEM]
        raw = bytes(i.bytes)
        k = raw.find(struct.pack("<I", old))
        if disps != [old] or k < 0:
            sys.exit("0x%08X %s %s: does not read 0x%08X" % (va, i.mnemonic, i.op_str, old))
        ops.append({"op": "patch", "va": "0x%08X" % (va + k), "insn": "0x%08X" % va,
                    "old": struct.pack("<I", old).hex().upper(),
                    "new": struct.pack("<I", new).hex().upper(),
                    "why": "tick rate: %s %s: %s (0x%08X -> 0x%08X)" % (i.mnemonic, i.op_str, what, old, new)})

    for va in PER_TICK_SITES:
        repoint(va, INV50, INV60, "per second to per tick, 1/60 as in the USA release")
    for va in PER_TICK_SITES_BY_REGION:
        repoint(va, INV50, INV60, "per second to per tick, 1/60 (by region, see tools/ts2_tick_rate.py)")
    for va in PER_SECOND_SITES:
        repoint(va, F50, F60, "ticks in a second, 60 as in the USA release")
    for va in PER_TICK_IMM_SITES:
        i = insn_at(va)
        raw = bytes(i.bytes)
        k = raw.find(struct.pack("<I", IMM_INV50))
        if k < 0:
            sys.exit("0x%08X %s %s: no immediate 1/50" % (va, i.mnemonic, i.op_str))
        ops.append({"op": "patch", "va": "0x%08X" % (va + k), "insn": "0x%08X" % va,
                    "old": struct.pack("<I", IMM_INV50).hex().upper(),
                    "new": struct.pack("<I", IMM_INV60).hex().upper(),
                    "why": "tick rate: %s %s: 1/50 to 1/60 as in the USA release" % (i.mnemonic, i.op_str)})
    return ops
