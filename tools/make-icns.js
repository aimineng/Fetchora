#!/usr/bin/env node
/*
 * make-icns.js - build app.icns (macOS) from the PNGs make-icons.ps1 generates.
 *
 * There is no iconutil / png2icns / ImageMagick dependency here: an .icns file
 * is a trivial container - an 'icns' magic, a big-endian total length, then a
 * sequence of <4-char type><uint32 length><payload> elements. Every element this
 * script writes is an ordinary PNG file (image/png), which is exactly what
 * modern macOS expects for icon sizes of 128 px and up, and is also accepted for
 * all the smaller ones (icp4/icp5).
 *
 * It is a pure repackaging step: the artwork is resources/app-<size>.png, so no
 * new image data is invented. Run it after make-icons.ps1 regenerates the PNGs:
 *
 *     node tools/make-icns.js
 *
 * Writes app.icns next to this file's parent directory (the repository root).
 */

'use strict';

const fs = require('fs');
const path = require('path');

const root = path.resolve(__dirname, '..');
const resDir = path.join(root, 'resources');

// [ostype, pixel size]. Only sizes this repository actually has artwork for are
// listed. icp4/icp5 are the legacy 16/32 px slots; ic07..ic14 are the modern
// ones and cover both the 1x and the @2x slot at their size - so a source PNG
// legitimately appears twice (icp5 and ic11 are both 32 px). 512/1024 px artwork
// (ic10/ic09) does not exist in the repository and is not invented here; macOS
// scales up from 256 px, which looks fine for an app icon.
const ELEMENTS = [
    ['icp4', 16],
    ['icp5', 32],
    ['ic07', 128],
    ['ic08', 256],
    ['ic11', 32],
    ['ic12', 64],
    ['ic13', 128],
    ['ic14', 256],
];

/** Reads a PNG and asserts its IHDR dimensions, so a mismatched name is caught. */
function readPng(size) {
    const file = path.join(resDir, `app-${size}.png`);
    const data = fs.readFileSync(file);
    const isPng = data.slice(0, 8).equals(Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]));
    if (!isPng) {
        throw new Error(`${file} is not a PNG`);
    }
    const width = data.readUInt32BE(16);
    const height = data.readUInt32BE(20);
    if (width !== size || height !== size) {
        throw new Error(`${file} is ${width}x${height}, expected ${size}x${size}`);
    }
    return data;
}

const chunks = [];
for (const [ostype, size] of ELEMENTS) {
    const png = readPng(size);
    const chunk = Buffer.alloc(8 + png.length);
    chunk.write(ostype, 0, 4, 'ascii');
    chunk.writeUInt32BE(8 + png.length, 4); // element length includes the header
    png.copy(chunk, 8);
    chunks.push({ ostype, size, chunk });
}

const body = Buffer.concat(chunks.map((c) => c.chunk));
const header = Buffer.alloc(8);
header.write('icns', 0, 4, 'ascii');
header.writeUInt32BE(8 + body.length, 4);

const out = path.join(root, 'app.icns');
fs.writeFileSync(out, Buffer.concat([header, body]));

console.log(`Wrote ${out} (${8 + body.length} bytes, ${chunks.length} elements)`);
for (const c of chunks) {
    console.log(`  ${c.ostype}  ${String(c.size).padStart(4)} px  ${c.chunk.length} bytes`);
}
