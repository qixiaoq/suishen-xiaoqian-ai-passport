import test from 'node:test';
import assert from 'node:assert/strict';
import { createServer } from 'node:http';
import { createApp, validateCasting, parseQrPayload } from './server.js';
import { dailyCalendar, birthPillars, meihuaAt } from './calendar.js';

test('compact QR payload carries all four entry modes', () => {
  assert.equal(parseQrPayload('d20261007').date, '2026-10-07');
  assert.deepEqual(parseQrPayload('l678967').lines, [6, 7, 8, 9, 6, 7]);
  assert.equal(parseQrPayload('l678967').baseLines, 42);
  assert.equal(parseQrPayload('l678967').changedLines, 51);
  assert.equal(parseQrPayload('b').mode, 'bazi');
  const gua = parseQrPayload('m202610071230');
  assert.deepEqual(gua, meihuaAt('202610071230'));
  assert.equal(gua.yearBranch, 7);
  assert.throws(() => parseQrPayload('d20260231'));
});

test('calendar facts come from deterministic rules and incomplete birth omits hour pillar', () => {
  const today = dailyCalendar('2026-10-07');
  assert.equal(today.lunarDate, '二〇二六年八月廿七');
  assert.equal(today.dayType, '黄道');
  assert.throws(() => dailyCalendar('2026-02-31'));
  const partial = birthPillars({ date: '1990-01-01', place: '宁波' });
  assert.equal(partial.year, '己巳');
  assert.equal(partial.hour, null);
  assert.equal(birthPillars({ date: '1990-01-01', hour: 10 }).hour, '癸巳');
});

test('phone setup reports AI availability and a compact almanac for the device', async () => {
  const app = createApp();
  await new Promise(resolve => app.listen(0, '127.0.0.1', resolve));
  try {
    const base = `http://127.0.0.1:${app.address().port}`;
    const config = await (await fetch(`${base}/api/config`)).json();
    assert.equal(config.aiReady, Boolean(process.env.AI_API_BASE_URL &&
      process.env.AI_API_KEY && process.env.AI_MODEL));
    const today = await (await fetch(`${base}/api/today?date=2026-10-07`)).json();
    assert.equal(today.date, '2026-10-07');
    assert.ok(today.yi.length && today.ji.length && today.lunarDate.length);
    assert.match(today.dayType, /^(黄道|黑道)$/);
  } finally {
    await new Promise(resolve => app.close(resolve));
  }
});

test('local setup enables interpretation without returning the API key', async () => {
  const app = createApp();
  await new Promise(resolve => app.listen(0, '127.0.0.1', resolve));
  try {
    const base = `http://127.0.0.1:${app.address().port}`;
    assert.equal((await fetch(`${base}/setup`)).status, 200);
    const invalid = await fetch(`${base}/api/local-config`, {
      method: 'POST', headers: { 'content-type': 'application/json' },
      body: JSON.stringify({ base: 'http://example.com/v1/', model: 'test', key: 'secret' }),
    });
    assert.equal(invalid.status, 400);
    const saved = await fetch(`${base}/api/local-config`, {
      method: 'POST', headers: { 'content-type': 'application/json' },
      body: JSON.stringify({ base: 'https://example.com/v1/', model: 'test', key: 'secret' }),
    });
    assert.equal(saved.status, 200);
    assert.deepEqual(await saved.json(), { ready: true });
    assert.deepEqual(await (await fetch(`${base}/api/config`)).json(), { aiReady: true });
  } finally {
    await new Promise(resolve => app.close(resolve));
  }
});

test('casting schema rejects inconsistent lines', () => {
  assert.equal(validateCasting({ mode: 'meihua', baseLines: 12, changedLines: 13, movingLine: 1 }), true);
  assert.equal(validateCasting({ mode: 'meihua', baseLines: 12, changedLines: 14, movingLine: 1 }), false);
  assert.equal(validateCasting({ mode: 'liuyao', lines: [6, 7, 8, 9, 6, 7] }), true);
  assert.equal(validateCasting({ mode: 'liuyao', lines: [6, 7, 8] }), false);
  assert.equal(validateCasting({ mode: 'daily', date: '2026-02-31' }), false);
});

test('device creates a short lived session and phone can read it', async () => {
  const old = process.env.DEVICE_TOKEN;
  process.env.DEVICE_TOKEN = 'local-test-token';
  const app = createApp();
  await new Promise(resolve => app.listen(0, '127.0.0.1', resolve));
  try {
    const base = `http://127.0.0.1:${app.address().port}`;
    const unauthorized = await fetch(`${base}/api/sessions`, {
      method: 'POST', body: JSON.stringify({ mode: 'daily', date: '2026-10-07' }),
    });
    assert.equal(unauthorized.status, 401);
    const created = await fetch(`${base}/api/sessions`, {
      method: 'POST',
      headers: { authorization: 'Bearer local-test-token', 'content-type': 'application/json' },
      body: JSON.stringify({ mode: 'liuyao', lines: [6, 7, 8, 9, 6, 7] }),
    });
    assert.equal(created.status, 201);
    const data = await created.json();
    assert.match(data.url, /\/s\/[A-Za-z0-9_-]+$/);
    const loaded = await fetch(`${base}/api/sessions/${data.id}`);
    assert.deepEqual((await loaded.json()).casting.lines, [6, 7, 8, 9, 6, 7]);
    const scanned = await fetch(`${base}/q/l678967`, { redirect: 'manual' });
    assert.equal(scanned.status, 303);
    const fromQr = await fetch(new URL(scanned.headers.get('location'), base));
    assert.equal(fromQr.status, 200);
  } finally {
    await new Promise(resolve => app.close(resolve));
    if (old === undefined) delete process.env.DEVICE_TOKEN;
    else process.env.DEVICE_TOKEN = old;
  }
});

test('scanned six-line QR reaches a plain-language AI response', async () => {
  const previous = Object.fromEntries(['AI_API_BASE_URL', 'AI_API_KEY', 'AI_MODEL']
    .map(key => [key, process.env[key]]));
  let suppliedCasting;
  const ai = createServer(async (req, res) => {
    assert.equal(req.url, '/v1/chat/completions');
    assert.equal(req.headers.authorization, 'Bearer local-test-key');
    let text = '';
    for await (const chunk of req) text += chunk;
    suppliedCasting = JSON.parse(JSON.parse(text).messages[1].content);
    res.writeHead(200, { 'content-type': 'application/json' });
    res.end(JSON.stringify({ choices: [{ message: { content: '先缓一缓，再核实眼前的信息。' } }] }));
  });
  await new Promise(resolve => ai.listen(0, '127.0.0.1', resolve));
  process.env.AI_API_BASE_URL = `http://127.0.0.1:${ai.address().port}/v1/`;
  process.env.AI_API_KEY = 'local-test-key';
  process.env.AI_MODEL = 'local-test-model';
  const app = createApp();
  await new Promise(resolve => app.listen(0, '127.0.0.1', resolve));
  try {
    const base = `http://127.0.0.1:${app.address().port}`;
    const opened = await fetch(`${base}/q/l678967`, { redirect: 'manual' });
    assert.equal(opened.status, 303);
    const session = opened.headers.get('location').split('/').at(-1);
    const response = await fetch(`${base}/api/sessions/${session}/interpret`, {
      method: 'POST', headers: { 'content-type': 'application/json' },
      body: JSON.stringify({ question: '这件事该怎么推进？', category: '工作与学业' }),
    });
    assert.equal(response.status, 200);
    assert.equal((await response.json()).result, '先缓一缓，再核实眼前的信息。');
    assert.deepEqual(suppliedCasting.sixLines.movingLines, [1, 4, 5]);
  } finally {
    await new Promise(resolve => app.close(resolve));
    await new Promise(resolve => ai.close(resolve));
    for (const [key, value] of Object.entries(previous)) {
      if (value === undefined) delete process.env[key];
      else process.env[key] = value;
    }
  }
});
