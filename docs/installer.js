// MinUIPlus web installer: the same job as tools/package/install.sh, done in the browser.
// It downloads the latest release, checks it, and copies it onto a MinUI card the person picks
// (File System Access API: Chrome, Edge and other Chromium browsers), backing up what it replaces.
// No UI in here, so it can be tested in Node with an in-memory folder.

const TEXT = new TextDecoder();
const ENC = new TextEncoder();

// files install.sh adds even when the card doesn't have them yet (new tools), everything else is only replaced
const NEW_FILE = [
	/^Tools\/rgb30\/(Romulus|Stats|Theme|TitleArt|Info|Update|Screenshots|Settings|SSH)\.pak\//,
	/^\.system\/res\/fallback(\.ttf|-OFL\.txt)$/,
	/^Tools\/rgb30\/Wi-Fi\.pak\/(wifi\.elf|launch-old\.sh)$/,
];
const NOT_ON_CARD = /^(install\.sh|revert\.sh|README\.txt|SHA256SUMS|VERSION)$/; // package files that are not copied

const ROMULUS_CFG = `# Romulus server for the Romulus tool. Edit this file on a computer.
# The url must be https:// (the token is sent with every request).
url=
token=

# Optional: file a platform from the server in a particular ROM folder,
# one line per platform, eg.
# map=gba:Game Boy Advance (MGBA)
`;
const UPDATE_CFG = `# Where Tools > Update looks for new builds.
repo=casualducko/MinUIPlus
# For a private repo: a GitHub token that can read it (fine-grained, Contents: read). Keep this file private.
token=
`;

export async function sha256Hex(bytes) {
	const d = await crypto.subtle.digest('SHA-256', bytes);
	return [...new Uint8Array(d)].map(b => b.toString(16).padStart(2, '0')).join('');
}

export async function gunzip(bytes) {
	const stream = new Blob([bytes]).stream().pipeThrough(new DecompressionStream('gzip'));
	return new Uint8Array(await new Response(stream).arrayBuffer());
}

function cstr(bytes, start, len) {
	let end = start;
	while (end < start + len && bytes[end] !== 0) end++;
	return TEXT.decode(bytes.subarray(start, end));
}

// a ustar tar file: [{path, data}] for the regular files
export function parseTar(bytes) {
	const files = [];
	let pos = 0;
	while (pos + 512 <= bytes.length) {
		if (bytes.subarray(pos, pos + 512).every(b => b === 0)) break;
		const name = cstr(bytes, pos, 100);
		const size = parseInt(cstr(bytes, pos + 124, 12).trim() || '0', 8);
		const type = String.fromCharCode(bytes[pos + 156] || 48);
		const prefix = cstr(bytes, pos + 345, 155);
		if (!Number.isFinite(size) || size < 0) throw new Error('the package is damaged (bad size in the archive)');
		const path = (prefix ? prefix + '/' : '') + name;
		pos += 512;
		if (type === '0') {
			if (pos + size > bytes.length) throw new Error('the package is damaged (cut short)');
			files.push({ path: path.replace(/^\.\//, ''), data: bytes.subarray(pos, pos + size) });
		}
		else if (type !== '5') throw new Error('the package has an entry this installer does not handle (type ' + type + ')');
		pos += Math.ceil(size / 512) * 512;
	}
	return files;
}

export function safePath(path) {
	return path !== '' && !path.startsWith('/') && !path.split('/').some(p => p === '..' || p === '');
}

// the package's own SHA256SUMS against what was unpacked; throws a sentence a person can read
export async function verifyPackage(files) {
	const byPath = new Map(files.map(f => [f.path, f]));
	for (const f of files) if (!safePath(f.path)) throw new Error('the package has an unsafe file name: ' + f.path);
	const sums = byPath.get('SHA256SUMS');
	if (!sums) throw new Error('the package has no SHA256SUMS');
	let checked = 0;
	for (const line of TEXT.decode(sums.data).split('\n')) {
		const m = /^([0-9a-f]{64})\s+\*?(.+)$/.exec(line.trim());
		if (!m) continue;
		const f = byPath.get(m[2].replace(/^\.\//, ''));
		if (!f) throw new Error('the package is missing ' + m[2]);
		if (await sha256Hex(f.data) !== m[1]) throw new Error('the package is damaged: ' + m[2] + ' does not match its checksum');
		checked++;
	}
	if (!checked) throw new Error('the package has an empty SHA256SUMS');
	return checked;
}

// The newest package: {tag, files}. GitHub does not let a web page download release files (no CORS), so
// tools/package/release.sh also puts the latest package in docs/latest/, next to this page, where it can.
// onStatus(text) says what it is doing; base is the folder the page is served from.
export async function downloadLatest(base = new URL('./latest/', import.meta.url).href, onStatus = () => {}, fetchFn = fetch) {
	onStatus('Looking for the latest release...');
	const info = await fetchFn(base + 'version.json');
	if (!info.ok) throw new Error(info.status === 404 ? 'No release has been published for this installer yet.' : 'Could not read the release information (' + info.status + ').');
	const v = await info.json();
	if (!v || !/^[\w.-]+\.tar\.gz$/.test(v.file || '') || !/^[0-9a-f]{64}$/.test(v.sha256 || '')) throw new Error('The release information is not valid.');
	onStatus('Downloading ' + v.tag + ' (' + (v.size / 1048576).toFixed(1) + ' MB)...');
	const r = await fetchFn(base + v.file);
	if (!r.ok) throw new Error('Downloading the package failed (' + r.status + ').');
	const bytes = new Uint8Array(await r.arrayBuffer());
	onStatus('Checking the download...');
	if (await sha256Hex(bytes) !== v.sha256) throw new Error('The download does not match its checksum. Try again.');
	const files = parseTar(await gunzip(bytes));
	const count = await verifyPackage(files);
	onStatus('Package ok: ' + count + ' files checked.');
	return { tag: v.tag, files };
}

// --- the card ---------------------------------------------------------------------------------

async function dirAt(root, path, create) { // path "a/b/c" below root; null if it isn't there and create is false
	let d = root;
	for (const part of path.split('/').filter(Boolean)) {
		try { d = await d.getDirectoryHandle(part, { create }); }
		catch (e) { if (!create && e.name === 'NotFoundError') return null; throw e; }
	}
	return d;
}
async function fileAt(root, path, create) {
	const i = path.lastIndexOf('/');
	const dir = await dirAt(root, i < 0 ? '' : path.slice(0, i), create);
	if (!dir) return null;
	try { return await dir.getFileHandle(path.slice(i + 1), { create }); }
	catch (e) { if (!create && (e.name === 'NotFoundError' || e.name === 'TypeMismatchError')) return null; throw e; }
}
async function readBytes(root, path) {
	const h = await fileAt(root, path, false);
	return h ? new Uint8Array(await (await h.getFile()).arrayBuffer()) : null;
}
async function writeBytes(root, path, data) {
	const h = await fileAt(root, path, true);
	const w = await h.createWritable();
	await w.write(data);
	await w.close();
}

// is this a MinUI card for this device? (the .system/rgb30/bin folder install.sh looks for)
export async function looksLikeMinUICard(root) {
	return !!(await dirAt(root, '.system/rgb30/bin', false));
}

// copies every file of a folder (and the folders in it) into the backup, for a tool folder that is retired
async function copyTree(root, from, to) {
	const dir = await dirAt(root, from, false);
	if (!dir) return false;
	for await (const [name, handle] of dir.entries()) {
		if (handle.kind === 'directory') await copyTree(root, from + '/' + name, to + '/' + name);
		else await writeBytes(root, to + '/' + name, await readBytes(root, from + '/' + name));
	}
	return true;
}

export function stamp(d = new Date()) {
	const p = n => String(n).padStart(2, '0');
	return d.getFullYear() + p(d.getMonth() + 1) + p(d.getDate()) + '-' + p(d.getHours()) + p(d.getMinutes()) + p(d.getSeconds());
}

// install.sh, on the picked folder. Returns {replaced, added, skipped, backup}.
export async function installOnCard(root, pkg, log = () => {}, now = new Date()) {
	if (!(await looksLikeMinUICard(root)))
		throw new Error('That folder has no .system/rgb30/bin in it, so it is not the MinUI card. Pick the card itself (the one with the .system folder).');
	const backup = '.system/rgb30/backup-before-fork-' + stamp(now);
	const out = { replaced: 0, added: 0, skipped: 0, backup };
	log('Backing up what it replaces to ' + backup);
	for (const f of pkg.files) {
		if (NOT_ON_CARD.test(f.path) || !/^(\.system|Tools|Emus)\//.test(f.path)) continue;
		const old = await readBytes(root, f.path);
		if (!old && !NEW_FILE.some(r => r.test(f.path))) { out.skipped++; log('  skip (not on the card): ' + f.path); continue; }
		if (old) await writeBytes(root, backup + '/' + f.path, old);
		await writeBytes(root, f.path, f.data);
		if (old) { out.replaced++; log('  replaced: ' + f.path); }
		else { out.added++; log('  added: ' + f.path); }
	}
	// the Settings tool used to be called Power
	if (await copyTree(root, 'Tools/rgb30/Power.pak', backup + '/Tools/rgb30/Power.pak')) {
		const tools = await dirAt(root, 'Tools/rgb30', false);
		await tools.removeEntry('Power.pak', { recursive: true });
		log('  retired the old Power tool (it is Settings now)');
	}
	// settings files the tools read: made once, never overwritten
	if (await dirAt(root, '.userdata/rgb30', false)) {
		if (!(await fileAt(root, '.userdata/rgb30/romulus.cfg', false))) { await writeBytes(root, '.userdata/rgb30/romulus.cfg', ENC.encode(ROMULUS_CFG)); log('  created .userdata/rgb30/romulus.cfg'); }
		if (!(await fileAt(root, '.userdata/rgb30/update.cfg', false))) { await writeBytes(root, '.userdata/rgb30/update.cfg', ENC.encode(UPDATE_CFG)); log('  created .userdata/rgb30/update.cfg'); }
	}
	await writeBytes(root, '.system/rgb30/fork-version.txt', ENC.encode(pkg.tag + '\n'));
	return out;
}
