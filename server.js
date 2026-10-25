'use strict';
const http = require('node:http');
const fs = require('node:fs/promises');
const path = require('node:path');
const crypto = require('node:crypto');
const { promisify } = require('node:util');

const HOST = '127.0.0.1';
const PORT = Number(process.env.ASA_PORT || 3000);
const ROOT = __dirname;
const ACCOUNT_FILE = path.join(ROOT, 'data', 'accounts.json');
const SESSION_MS = 30 * 60 * 1000;
const scrypt = promisify(crypto.scrypt);
const sessions = new Map();
const attempts = new Map();

function json(res, code, value, extra = {}) {
  res.writeHead(code, { 'Content-Type': 'application/json; charset=utf-8', 'Cache-Control': 'no-store', ...extra });
  res.end(JSON.stringify(value));
}
function cookie(res, token, maxAge) {
  res.setHeader('Set-Cookie', 'asa_session=' + token + '; HttpOnly; SameSite=Strict; Path=/; Max-Age=' + maxAge);
}
function getCookie(req, name) {
  const parts = (req.headers.cookie || '').split(';');
  for (const part of parts) {
    const i = part.indexOf('=');
    if (i > 0 && part.slice(0, i).trim() === name) return decodeURIComponent(part.slice(i + 1).trim());
  }
  return '';
}
function getSession(req) {
  const id = getCookie(req, 'asa_session');
  const session = sessions.get(id);
  if (!session || session.expiresAt < Date.now()) {
    if (id) sessions.delete(id);
    return null;
  }
  session.expiresAt = Date.now() + SESSION_MS;
  return session;
}
function allowedOrigin(req) {
  const origin = req.headers.origin;
  return !origin || origin === 'http://' + HOST + ':' + PORT || origin === 'http://localhost:' + PORT;
}
async function body(req, limit = 8192) {
  let data = '';
  for await (const chunk of req) {
    data += chunk;
    if (data.length > limit) throw new Error('Request too large');
  }
  return data ? JSON.parse(data) : {};
}
async function readAccounts() {
  try { return JSON.parse(await fs.readFile(ACCOUNT_FILE, 'utf8')); }
  catch (e) { if (e.code === 'ENOENT') return {}; throw e; }
}
async function saveAccounts(accounts) {
  await fs.mkdir(path.dirname(ACCOUNT_FILE), { recursive: true });
  const tmp = ACCOUNT_FILE + '.tmp';
  await fs.writeFile(tmp, JSON.stringify(accounts, null, 2), { mode: 0o600 });
  await fs.rename(tmp, ACCOUNT_FILE);
}
function rateLimited(key) {
  const now = Date.now();
  const item = attempts.get(key) || { count: 0, until: now + 60000 };
  if (item.until < now) { item.count = 0; item.until = now + 60000; }
  item.count++;
  attempts.set(key, item);
  return item.count > 8;
}
function newSession(username) {
  const id = crypto.randomBytes(32).toString('base64url');
  sessions.set(id, { username, asaVerified: false, expiresAt: Date.now() + SESSION_MS });
  return id;
}
async function handleApi(req, res, url) {
  if (!allowedOrigin(req)) return json(res, 403, { error: 'Request origin not allowed.' });
  if (req.method === 'GET' && url.pathname === '/api/session') {
    const s = getSession(req);
    return json(res, 200, s ? { authenticated: true, username: s.username, asaVerified: s.asaVerified } : { authenticated: false });
  }
  if (req.method === 'POST' && url.pathname === '/api/register') {
    if (rateLimited(req.socket.remoteAddress || 'local')) return json(res, 429, { error: 'Too many attempts. Wait one minute and try again.' });
    let data;
    try { data = await body(req); } catch (_) { return json(res, 400, { error: 'Invalid request.' }); }
    const username = String(data.username || '').trim();
    const password = String(data.password || '');
    if (!/^[A-Za-z0-9_.-]{3,32}$/.test(username)) return json(res, 400, { error: 'Use a username with 3–32 letters, numbers, dots, dashes, or underscores.' });
    if (password.length < 8 || password.length > 128) return json(res, 400, { error: 'Use a password between 8 and 128 characters.' });
    const accounts = await readAccounts();
    if (Object.keys(accounts).length > 0) return json(res, 409, { error: 'This course demo supports one local account. Sign in with the account already created.' });
    const salt = crypto.randomBytes(16);
    const hash = await scrypt(password, salt, 64, { N: 32768, r: 8, p: 1, maxmem: 128 * 1024 * 1024 });
    accounts[username] = { salt: salt.toString('hex'), hash: hash.toString('hex'), createdAt: new Date().toISOString() };
    await saveAccounts(accounts);
    const id = newSession(username);
    cookie(res, id, SESSION_MS / 1000);
    return json(res, 201, { authenticated: true, username, asaVerified: false });
  }
  if (req.method === 'POST' && url.pathname === '/api/login') {
    if (rateLimited(req.socket.remoteAddress || 'local')) return json(res, 429, { error: 'Too many attempts. Wait one minute and try again.' });
    let data;
    try { data = await body(req); } catch (_) { return json(res, 400, { error: 'Invalid request.' }); }
    const username = String(data.username || '').trim();
    const password = String(data.password || '');
    const accounts = await readAccounts();
    const account = accounts[username];
    if (!account) return json(res, 401, { error: 'Username or password is incorrect.' });
    const salt = Buffer.from(account.salt, 'hex');
    const expected = Buffer.from(account.hash, 'hex');
    const actual = Buffer.from(await scrypt(password, salt, expected.length, { N: 32768, r: 8, p: 1, maxmem: 128 * 1024 * 1024 }));
    if (actual.length !== expected.length || !crypto.timingSafeEqual(actual, expected)) return json(res, 401, { error: 'Username or password is incorrect.' });
    const id = newSession(username);
    cookie(res, id, SESSION_MS / 1000);
    return json(res, 200, { authenticated: true, username, asaVerified: false });
  }
  if (req.method === 'POST' && url.pathname === '/api/asa-verified') {
    const s = getSession(req);
    if (!s) return json(res, 401, { error: 'Sign in again to continue.' });
    let data;
    try { data = await body(req); } catch (_) { return json(res, 400, { error: 'Invalid request.' }); }
    if (data.result !== 'ACCEPT') return json(res, 403, { error: 'ASA did not accept the gesture.' });
    s.asaVerified = true;
    return json(res, 200, { authenticated: true, username: s.username, asaVerified: true });
  }
  if (req.method === 'POST' && url.pathname === '/api/logout') {
    const id = getCookie(req, 'asa_session');
    if (id) sessions.delete(id);
    cookie(res, '', 0);
    return json(res, 200, { loggedOut: true });
  }
  return json(res, 404, { error: 'Not found.' });
}
const server = http.createServer(async (req, res) => {
  res.setHeader('X-Content-Type-Options', 'nosniff');
  res.setHeader('Referrer-Policy', 'no-referrer');
  res.setHeader('Content-Security-Policy', "default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; connect-src 'self'; img-src 'self' data:; object-src 'none'; base-uri 'self'; frame-ancestors 'none'; form-action 'self'");
  const url = new URL(req.url, 'http://' + HOST + ':' + PORT);
  if (url.pathname.startsWith('/api/')) return handleApi(req, res, url);
  if (req.method !== 'GET') return json(res, 405, { error: 'Method not allowed.' });
  if (url.pathname === '/' || url.pathname === '/dashboard' || url.pathname === '/dashboard/') url.pathname = '/dashboard/index.html';
  const allowed = new Set(['/dashboard/index.html']);
  if (!allowed.has(url.pathname)) return json(res, 404, { error: 'Not found.' });
  try {
    const html = await fs.readFile(path.join(ROOT, url.pathname));
    res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8', 'Cache-Control': 'no-store' });
    res.end(html);
  } catch (_) { json(res, 500, { error: 'Dashboard file is missing.' }); }
});
server.listen(PORT, HOST, () => console.log('ASA demo ready at http://localhost:' + PORT));
server.on('error', e => { console.error(e.message); process.exitCode = 1; });
