#!/usr/bin/env python3
"""
Binds Pd externals' Pd symbols to one libpd, instead of "whoever exports it".

Pd externals are linked flat-namespace (`-undefined dynamic_lookup`, and no
MH_TWOLEVEL): every Pd function they call -- class_new, gensym, outlet_new,
post, ... -- is looked up by searching all images in the process in load
order. In the app, and in most plugin hosts, libpd is the only image with
those names. Max-based hosts (SoundDesk) export Max's C API, which uses the
same names, from their own executable, which is searched first. The
externals then register their classes with Max, Pd never sees them, and
every one of them fails with "maximum object loading depth 1000 reached".

This rewrites each external so the symbols libpd exports are bound to it:

  - adds an LC_LOAD_DYLIB for libpd, by the path given (for the plugin:
    @loader_path/../../../Frameworks/libpd.dylib),
  - lazy binds: each entry has its own "flat lookup" opcode
    (SET_DYLIB_SPECIAL_IMM -2, one byte), rewritten in place to
    SET_DYLIB_ORDINAL_IMM n (one byte), so the stub helpers' offsets hold,
  - non-lazy binds: one opcode can cover Pd and C++ runtime symbols alike,
    so the stream is decoded into single binds, Pd's are given libpd's
    ordinal, and it is encoded again at the end of __LINKEDIT,
  - marks the image two-level (MH_TWOLEVEL), without which dyld ignores
    ordinals. Every symbol libpd does not export keeps its flat lookup, as
    before.

No rebuild is needed (abl_link~ must stay the prebuilt binary). Universal
files are split, patched per architecture and merged again; `--arch A`
keeps only that architecture (the Faust externals' x86_64 slices have no
header room for the extra load command, and the plugin is built for one). Classic dyld
info only; chained fixups are refused. The code signature is removed: sign
the result again.

  bind-externals-to-libpd.py [--arch A] <libpd.dylib> <load path for libpd> <external>...
"""

import os
import shutil
import struct
import subprocess
import sys
import tempfile

MH_MAGIC_64 = 0xFEEDFACF
MH_TWOLEVEL = 0x80
LC_REQ_DYLD = 0x80000000
LC_SEGMENT_64 = 0x19
LC_LOAD_DYLIB = 0xC
DYLIB_CMDS = {LC_LOAD_DYLIB, 0x18 | LC_REQ_DYLD, 0x1F | LC_REQ_DYLD, 0x20, 0x23 | LC_REQ_DYLD}
LC_DYLD_INFO, LC_DYLD_INFO_ONLY = 0x22, 0x22 | LC_REQ_DYLD
LC_DYLD_CHAINED_FIXUPS = 0x34 | LC_REQ_DYLD
LC_CODE_SIGNATURE = 0x1D

FLAT = -2                       # BIND_SPECIAL_DYLIB_FLAT_LOOKUP
FLAT_OPCODE = 0x30 | (FLAT & 0x0F)
POINTER = 8


def uleb(data, i):
    result = shift = 0
    while True:
        b = data[i]; i += 1
        result |= (b & 0x7F) << shift
        shift += 7
        if b < 0x80:
            return result, i


def sleb(data, i):
    result = shift = 0
    while True:
        b = data[i]; i += 1
        result |= (b & 0x7F) << shift
        shift += 7
        if b < 0x80:
            if b & 0x40:
                result -= 1 << shift
            return result, i


def enc_uleb(value):
    out = bytearray()
    while True:
        b = value & 0x7F
        value >>= 7
        out.append(b | (0x80 if value else 0))
        if not value:
            return out


def enc_sleb(value):
    out = bytearray()
    while True:
        b = value & 0x7F
        value >>= 7
        done = (value == 0 and not b & 0x40) or (value == -1 and b & 0x40)
        out.append(b | (0 if done else 0x80))
        if done:
            return out


def decode_binds(data, start, size):
    """The non-lazy bind stream as single binds:
    (segment, offset, type, addend, ordinal, symbol, flags)."""
    i, end = start, start + size
    seg = off = addend = ordinal = flags = 0
    kind, symbol, binds = 1, None, []

    def bind():
        binds.append((seg, off, kind, addend, ordinal, symbol, flags))

    while i < end:
        op, imm = data[i] & 0xF0, data[i] & 0x0F
        i += 1
        if op == 0x00:                                  # DONE
            break
        elif op == 0x10:                                # SET_DYLIB_ORDINAL_IMM
            ordinal = imm
        elif op == 0x20:                                # SET_DYLIB_ORDINAL_ULEB
            ordinal, i = uleb(data, i)
        elif op == 0x30:                                # SET_DYLIB_SPECIAL_IMM
            ordinal = (imm | ~0x0F) if imm else 0
        elif op == 0x40:                                # SET_SYMBOL_TRAILING_FLAGS_IMM
            j = data.index(0, i)
            symbol, flags, i = data[i:j].decode(), imm, j + 1
        elif op == 0x50:                                # SET_TYPE_IMM
            kind = imm
        elif op == 0x60:                                # SET_ADDEND_SLEB
            addend, i = sleb(data, i)
        elif op == 0x70:                                # SET_SEGMENT_AND_OFFSET_ULEB
            seg = imm
            off, i = uleb(data, i)
        elif op == 0x80:                                # ADD_ADDR_ULEB
            delta, i = uleb(data, i)
            off = (off + delta) & 0xFFFFFFFFFFFFFFFF
        elif op == 0x90:                                # DO_BIND
            bind(); off += POINTER
        elif op == 0xA0:                                # DO_BIND_ADD_ADDR_ULEB
            delta, i = uleb(data, i)
            bind(); off = (off + POINTER + delta) & 0xFFFFFFFFFFFFFFFF
        elif op == 0xB0:                                # DO_BIND_ADD_ADDR_IMM_SCALED
            bind(); off += POINTER + imm * POINTER
        elif op == 0xC0:                                # DO_BIND_ULEB_TIMES_SKIPPING_ULEB
            count, i = uleb(data, i)
            skip, i = uleb(data, i)
            for _ in range(count):
                bind(); off += POINTER + skip
        else:
            raise ValueError("unsupported bind opcode 0x%02x" % data[i - 1])
    return binds


def encode_binds(binds):
    out = bytearray()
    for seg, off, kind, addend, ordinal, symbol, flags in binds:
        if ordinal <= 0:
            out.append(0x30 | (ordinal & 0x0F))
        elif ordinal < 16:
            out.append(0x10 | ordinal)
        else:
            out += b"\x20" + enc_uleb(ordinal)
        out += bytes([0x40 | flags]) + symbol.encode() + b"\0"
        out.append(0x50 | kind)
        out += b"\x60" + enc_sleb(addend)
        out += bytes([0x70 | seg]) + enc_uleb(off)
        out.append(0x90)
    out.append(0x00)
    while len(out) % POINTER:
        out.append(0x00)
    return out


def patch_lazy(data, start, size, ordinal, exported):
    """Lazy entries each set their own dylib; rewrite the flat ones in place."""
    i, end, rebound, flat_at = start, start + size, set(), None
    while i < end:
        op = data[i] & 0xF0
        here = i; i += 1
        if op in (0x00, 0x10, 0x90):                    # DONE, SET_DYLIB_ORDINAL_IMM, DO_BIND
            flat_at = None
        elif op == 0x20:
            _, i = uleb(data, i); flat_at = None
        elif op == 0x30:
            flat_at = here if data[here] == FLAT_OPCODE else None
        elif op == 0x40:
            j = data.index(0, i)
            symbol = data[i:j].decode(); i = j + 1
            if flat_at is not None and symbol in exported:
                data[flat_at] = 0x10 | ordinal
                rebound.add(symbol)
        elif op in (0x60, 0x70, 0x80):
            _, i = uleb(data, i)
        elif op == 0x50:
            pass
        else:
            raise ValueError("unsupported lazy bind opcode 0x%02x" % data[here])
    return rebound


def patch_thin(path, load_path, exported):
    data = bytearray(open(path, "rb").read())
    magic, _, _, _, ncmds, sizeofcmds, flags, _ = struct.unpack_from("<IiiIIIII", data, 0)
    if magic != MH_MAGIC_64:
        raise ValueError("not a 64-bit Mach-O file")

    p, dylibs, info_at, linkedit_at, first_section = 32, 0, None, None, len(data)
    for _ in range(ncmds):
        cmd, cmdsize = struct.unpack_from("<II", data, p)
        if cmd in DYLIB_CMDS:
            dylibs += 1
            name_off = struct.unpack_from("<I", data, p + 8)[0]
            if data[p + name_off:data.index(0, p + name_off)].decode() == load_path:
                return None                             # done before
        elif cmd in (LC_DYLD_INFO, LC_DYLD_INFO_ONLY):
            info_at = p
        elif cmd == LC_DYLD_CHAINED_FIXUPS:
            raise ValueError("chained fixups are not supported")
        elif cmd == LC_CODE_SIGNATURE:
            raise ValueError("still signed (remove the signature first)")
        elif cmd == LC_SEGMENT_64:
            if data[p + 8:p + 24].rstrip(b"\0") == b"__LINKEDIT":
                linkedit_at = p
            for s in range(struct.unpack_from("<I", data, p + 64)[0]):
                offset = struct.unpack_from("<I", data, p + 72 + s * 80 + 48)[0]
                if offset:
                    first_section = min(first_section, offset)
        p += cmdsize
    if info_at is None or linkedit_at is None:
        raise ValueError("no dyld info or __LINKEDIT")

    ordinal = dylibs + 1
    if ordinal > 15:
        raise ValueError("too many dylibs for a one-byte ordinal")

    _, _, bind_off, bind_size, _, _, lazy_off, lazy_size, _, _ = struct.unpack_from("<10I", data, info_at + 8)

    # Lazy binds, in place.
    rebound = patch_lazy(data, lazy_off, lazy_size, ordinal, exported)

    # Non-lazy binds, re-encoded at the end of __LINKEDIT.
    binds = decode_binds(data, bind_off, bind_size)
    rebinds = [b[:4] + (ordinal,) + b[5:] if b[4] == FLAT and b[5] in exported else b for b in binds]
    rebound.update(b[5] for b, r in zip(binds, rebinds) if b != r)
    if rebinds != binds:
        _, _, vmsize, fileoff, filesize = struct.unpack_from("<16sQQQQ", data, linkedit_at + 8)
        if fileoff + filesize != len(data):
            raise ValueError("__LINKEDIT does not end the file")
        while len(data) % POINTER:
            data.append(0)
        stream = encode_binds(rebinds)
        new_off = len(data)
        data += stream
        filesize = len(data) - fileoff
        page = 0x4000
        struct.pack_into("<Q", data, linkedit_at + 32, max(vmsize, (filesize + page - 1) // page * page))
        struct.pack_into("<Q", data, linkedit_at + 48, filesize)
        struct.pack_into("<II", data, info_at + 16, new_off, len(stream))

    if not rebound:
        raise ValueError("no Pd symbols to bind")

    # The load command, in the header's padding.
    name = load_path.encode() + b"\0"
    cmdsize = (24 + len(name) + 7) & ~7
    if 32 + sizeofcmds + cmdsize > first_section:
        raise ValueError("no room for another load command")
    cmd = struct.pack("<IIIIII", LC_LOAD_DYLIB, cmdsize, 24, 2, 0x10000, 0x10000) + name
    data[32 + sizeofcmds:32 + sizeofcmds + cmdsize] = cmd.ljust(cmdsize, b"\0")
    struct.pack_into("<III", data, 16, ncmds + 1, sizeofcmds + cmdsize, flags | MH_TWOLEVEL)

    open(path, "wb").write(data)
    return len(rebound)


def run(*args):
    return subprocess.run(args, check=True, capture_output=True, text=True).stdout


def main():
    args, only_arch = sys.argv[1:], None
    if args[:1] == ["--arch"]:
        only_arch, args = args[1], args[2:]
    if len(args) < 3:
        sys.exit(__doc__)
    dylib, load_path, externals = args[0], args[1], args[2:]
    exported = set(run("nm", "-gU", "-j", dylib).split())

    for path in externals:
        name = os.path.basename(path)
        with tempfile.TemporaryDirectory() as tmp:
            work = os.path.join(tmp, name)
            shutil.copy(path, work)
            subprocess.run(["codesign", "--remove-signature", work], capture_output=True)
            archs = run("lipo", "-archs", work).split()
            if only_arch is not None:
                if only_arch not in archs:
                    sys.exit("%s: no %s slice" % (name, only_arch))
                if len(archs) > 1:
                    run("lipo", work, "-thin", only_arch, "-output", work)
                archs = [only_arch]
            thins = []
            for arch in archs:
                thin = os.path.join(tmp, arch)
                if len(archs) > 1:
                    run("lipo", work, "-thin", arch, "-output", thin)
                else:
                    shutil.copy(work, thin)
                thins.append(thin)
            counts = [patch_thin(t, load_path, exported) for t in thins]
            if all(c is None for c in counts):
                print("%s: already bound" % name)
                continue
            if len(thins) > 1:
                run("lipo", "-create", *thins, "-output", work)
            else:
                shutil.copy(thins[0], work)
            shutil.copy(work, path)
            print("%s: %s Pd symbols bound to %s" % (name, "/".join(str(c) for c in counts), load_path))


if __name__ == "__main__":
    main()
