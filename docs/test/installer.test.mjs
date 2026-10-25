// node docs/test/installer.test.mjs <MinUIPlus-*.tar.gz> : the installer against a real package and an in-memory card
import { readFileSync } from 'node:fs';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { parseTar, gunzip, verifyPackage, installOnCard, downloadLatest, looksLikeMinUICard, safePath } from '../installer.js';

// a folder that behaves like a FileSystemDirectoryHandle
class Dir {
	constructor(name = '') { this.kind = 'directory'; this.name = name; this.kids = new Map(); }
	async getDirectoryHandle(n, { create } = {}) {
		let k = this.kids.get(n);
		if (!k) { if (!create) throw Object.assign(new Error('nf'), { name: 'NotFoundError' }); k = new Dir(n); this.kids.set(n, k); }
		if (k.kind !== 'directory') throw Object.assign(new Error('tm'), { name: 'TypeMismatchError' });
		return k;
	}
	async getFileHandle(n, { create } = {}) {
		let k = this.kids.get(n);
		if (!k) { if (!create) throw Object.assign(new Error('nf'), { name: 'NotFoundError' }); k = new File_(n); this.kids.set(n, k); }
		if (k.kind !== 'file') throw Object.assign(new Error('tm'), { name: 'TypeMismatchError' });
		return k;
	}
	async removeEntry(n) { this.kids.delete(n); }
	async *entries() { for (const e of this.kids) yield e; }
}
class File_ {
	constructor(name) { this.kind = 'file'; this.name = name; this.data = new Uint8Array(); }
	async getFile() { const d = this.data; return { arrayBuffer: async () => d.buffer.slice(d.byteOffset, d.byteOffset + d.byteLength) }; }
	async createWritable() { const f = this; return { write: async d => { f.data = new Uint8Array(d); }, close: async () => {} }; }
}
async function put(root, path, text) {
	const parts = path.split('/'); let d = root;
	for (const p of parts.slice(0, -1)) d = await d.getDirectoryHandle(p, { create: true });
	const f = await d.getFileHandle(parts.at(-1), { create: true });
	f.data = new TextEncoder().encode(text);
}
async function get(root, path) {
	let d = root; const parts = path.split('/');
	for (const p of parts.slice(0, -1)) d = await d.getDirectoryHandle(p);
	return new TextDecoder().decode((await d.getFileHandle(parts.at(-1))).data);
}
const has = async (root, path) => { try { await get(root, path); return true; } catch { return false; } };

const tgz = readFileSync(process.argv[2]);
const files = parseTar(await gunzip(tgz));
const count = await verifyPackage(files);
assert.ok(count > 20, 'checksums verified: ' + count);
console.log('ok   package parsed and verified (' + files.length + ' files, ' + count + ' checksummed)');

// a damaged copy is refused
const bad = files.map(f => ({ ...f }));
const victim = bad.find(f => f.path.endsWith('minui.elf'));
victim.data = new Uint8Array([1, 2, 3]);
await assert.rejects(verifyPackage(bad), /damaged/);
await assert.rejects(verifyPackage([...files, { path: '../evil', data: new Uint8Array() }]), /unsafe/);
assert.ok(!safePath('/etc/passwd') && !safePath('a/../b') && safePath('Tools/rgb30/Info.pak/info.elf'));
console.log('ok   damaged and unsafe packages refused');

// a card with old MinUI on it
const card = new Dir('card');
assert.equal(await looksLikeMinUICard(card), false);
await assert.rejects(installOnCard(card, { tag: 'v1', files }), /not the MinUI card/);
await put(card, '.system/rgb30/bin/minui.elf', 'OLD MINUI');
await put(card, 'Tools/rgb30/Clock.pak/clock.elf', 'OLD CLOCK');
await put(card, 'Tools/rgb30/Power.pak/launch.sh', 'old power');
await put(card, 'Tools/rgb30/Theme.pak/launch.sh', 'old theme');
await put(card, 'Tools/rgb30/TitleArt.pak/launch.sh', 'old titleart');
await put(card, '.userdata/rgb30/sleep-timeout', '60\n');
await put(card, 'Roms/GBA/game.gba', 'ROM');
const pkg = { tag: 'v2099.01.01.0000', files };
const log = [];
const r = await installOnCard(card, pkg, s => log.push(s), new Date(2026, 9, 25, 12, 30, 5));
assert.equal(await get(card, '.system/rgb30/backup-before-fork-20261025-123005/.system/rgb30/bin/minui.elf'), 'OLD MINUI');
assert.equal(await get(card, '.system/rgb30/backup-before-fork-20261025-123005/Tools/rgb30/Clock.pak/clock.elf'), 'OLD CLOCK');
assert.equal(await get(card, '.system/rgb30/backup-before-fork-20261025-123005/Tools/rgb30/Power.pak/launch.sh'), 'old power');
assert.ok(!(await has(card, 'Tools/rgb30/Power.pak/launch.sh')), 'old Power tool retired');
assert.equal(await get(card, '.system/rgb30/backup-before-fork-20261025-123005/Tools/rgb30/Theme.pak/launch.sh'), 'old theme');
assert.equal(await get(card, '.system/rgb30/backup-before-fork-20261025-123005/Tools/rgb30/TitleArt.pak/launch.sh'), 'old titleart');
assert.ok(!(await has(card, 'Tools/rgb30/Theme.pak/launch.sh')) && !(await has(card, 'Tools/rgb30/TitleArt.pak/launch.sh')), 'the renamed tools are retired');
assert.ok(await has(card, 'Tools/rgb30/Themes.pak/launch.sh') && await has(card, 'Tools/rgb30/ASCIIArt.pak/launch.sh'), 'the new names are added');
assert.ok(await has(card, 'Tools/rgb30/Info.pak/info.elf'), 'a new tool is added');
assert.ok(await has(card, 'Tools/rgb30/Update.pak/launch.sh'), 'the Update tool is added');
assert.ok(!(await has(card, 'install.sh')) && !(await has(card, 'SHA256SUMS')), 'package files are not copied');
assert.equal(await get(card, '.system/rgb30/fork-version.txt'), 'v2099.01.01.0000\n');
assert.match(await get(card, '.userdata/rgb30/update.cfg'), /repo=casualducko\/MinUIPlus/);
assert.match(await get(card, '.userdata/rgb30/romulus.cfg'), /url=/);
assert.equal(await get(card, '.userdata/rgb30/sleep-timeout'), '60\n');
assert.equal(await get(card, 'Roms/GBA/game.gba'), 'ROM');
assert.ok(r.replaced >= 2 && r.added >= 5 && r.skipped >= 0, JSON.stringify(r));
console.log('ok   install: ' + r.replaced + ' replaced, ' + r.added + ' added, ' + r.skipped + ' skipped; backups, new tools, settings untouched');

// a settings file that exists is never overwritten; installing twice works
await put(card, '.userdata/rgb30/update.cfg', 'repo=mine/fork\n');
await installOnCard(card, pkg, () => {}, new Date(2026, 9, 25, 12, 31, 0));
assert.equal(await get(card, '.userdata/rgb30/update.cfg'), 'repo=mine/fork\n');
console.log('ok   a second install keeps update.cfg');

// the download, from the page's own latest/ folder
const sha = createHash('sha256').update(tgz).digest('hex');
const meta = { tag: 'v9', file: 'MinUIPlus-v9.tar.gz', sha256: sha, size: tgz.length };
const fake = (m) => async (url) => {
	const ok = (body) => ({ ok: true, status: 200, json: async () => JSON.parse(new TextDecoder().decode(body)), arrayBuffer: async () => body.buffer.slice(body.byteOffset, body.byteOffset + body.byteLength) });
	if (url === 'http://x/latest/version.json') return ok(new TextEncoder().encode(JSON.stringify(m)));
	if (url === 'http://x/latest/MinUIPlus-v9.tar.gz') return ok(new Uint8Array(tgz));
	return { ok: false, status: 404 };
};
const dl = await downloadLatest('http://x/latest/', () => {}, fake(meta));
assert.equal(dl.tag, 'v9');
assert.equal(dl.files.length, files.length);
await assert.rejects(downloadLatest('http://x/latest/', () => {}, fake({ ...meta, sha256: '0'.repeat(64) })), /checksum/);
await assert.rejects(downloadLatest('http://x/latest/', () => {}, fake({ ...meta, file: '../../evil.tar.gz' })), /not valid/);
await assert.rejects(downloadLatest('http://y/latest/', () => {}, fake(meta)), /No release/);
console.log('ok   download: checked against its sha256; bad checksum, bad file name and missing release refused');
console.log('\nall installer tests passed');
