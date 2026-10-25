import { createServer } from 'node:http';
import { randomBytes } from 'node:crypto';
import { readFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';
import { birthPillars, dailyCalendar, meihuaAt } from './calendar.js';

const root = dirname(fileURLToPath(import.meta.url));
const sessions = new Map();
const SESSION_MS = 30 * 60 * 1000;
const MAX_BODY = 8192;
const validModes = new Set(['daily', 'meihua', 'liuyao', 'bazi']);

export function sixLineReading(lines) {
  if (!Array.isArray(lines) || lines.length !== 6 ||
      !lines.every(v => Number.isInteger(v) && v >= 6 && v <= 9)) {
    throw new Error('无效六爻记录');
  }
  let baseLines = 0;
  let changedLines = 0;
  const movingLines = [];
  lines.forEach((value, index) => {
    const yang = value % 2 === 1;
    const moving = value === 6 || value === 9;
    if (yang) baseLines |= 1 << index;
    if (yang !== moving) changedLines |= 1 << index;
    if (moving) movingLines.push(index + 1);
  });
  return { baseLines, changedLines, movingLines, order: '自下而上，初爻为第1位' };
}

// Compact QR payloads keep the device independent of a server-side device token.
// The visitor's phone creates the short-lived session when it opens the link.
export function parseQrPayload(value) {
  if (typeof value !== 'string' || value.length > 32) throw new Error('无效二维码');
  if (value === 'b') return { mode: 'bazi' };
  if (/^d\d{8}$/.test(value)) {
    const date = `${value.slice(1, 5)}-${value.slice(5, 7)}-${value.slice(7, 9)}`;
    dailyCalendar(date);
    return { mode: 'daily', date };
  }
  if (/^m\d{12}$/.test(value)) return meihuaAt(value.slice(1));
  if (/^l[6-9]{6}$/.test(value)) {
    const lines = [...value.slice(1)].map(Number);
    return { mode: 'liuyao', lines, ...sixLineReading(lines) };
  }
  throw new Error('无效二维码');
}

function cleanText(value, max) {
  return typeof value === 'string' ? value.trim().slice(0, max) : '';
}

export function validateCasting(data) {
  if (!data || !validModes.has(data.mode)) return false;
  if (data.mode === 'daily') {
    try { dailyCalendar(data.date); return true; } catch { return false; }
  }
  if (data.mode === 'bazi') return true;
  if (data.mode === 'meihua') {
    return Number.isInteger(data.baseLines) && data.baseLines >= 0 &&
      data.baseLines <= 63 && Number.isInteger(data.changedLines) &&
      data.changedLines >= 0 && data.changedLines <= 63 &&
      Number.isInteger(data.movingLine) && data.movingLine >= 1 &&
      data.movingLine <= 6 &&
      (data.baseLines ^ data.changedLines) === (1 << (data.movingLine - 1));
  }
  return Array.isArray(data.lines) && data.lines.length === 6 &&
    data.lines.every(v => Number.isInteger(v) && v >= 6 && v <= 9);
}

function json(res, code, data) {
  res.writeHead(code, {
    'content-type': 'application/json; charset=utf-8',
    'cache-control': 'no-store',
    'x-content-type-options': 'nosniff',
  });
  res.end(JSON.stringify(data));
}

async function body(req) {
  let size = 0;
  const chunks = [];
  for await (const chunk of req) {
    size += chunk.length;
    if (size > MAX_BODY) throw new Error('请求内容过长');
    chunks.push(chunk);
  }
  return JSON.parse(Buffer.concat(chunks).toString('utf8'));
}

function getSession(id) {
  const item = sessions.get(id);
  if (!item) return null;
  if (item.expires < Date.now()) {
    sessions.delete(id);
    return null;
  }
  return item;
}

async function interpret(session, input, aiConfig) {
  const { base, key, model } = aiConfig;
  if (!base || !key || !model) throw new Error('AI 服务尚未配置');
  const question = cleanText(input.question, 500);
  const category = cleanText(input.category, 40);
  const birth = session.casting.mode === 'bazi' ? birthPillars(input.birth) : undefined;
  const daily = session.casting.mode === 'daily' ? dailyCalendar(session.casting.date) : undefined;
  const sixLines = session.casting.mode === 'liuyao' ? sixLineReading(session.casting.lines) : undefined;
  const payload = {
    mode: session.casting.mode,
    casting: session.casting,
    question,
    category,
    birth,
    daily,
    sixLines,
  };
  const response = await fetch(new URL('chat/completions', base.endsWith('/') ? base : `${base}/`), {
    method: 'POST',
    headers: {
      authorization: `Bearer ${key}`,
      'content-type': 'application/json',
    },
    body: JSON.stringify({
      model,
      temperature: 0.5,
      messages: [
        { role: 'system', content: '你是传统易学文化讲解员。只解释用户提供的已计算数据，不自行更改卦象、爻位或四柱。六爻数组从初爻到上爻排列，6和9为动爻；卦象整数的最低位是初爻。请用简体中文输出四小段：【一句话看法】【为什么这么说】【可做的一件小事】【术语小注】。前三段使用日常语言，先回答用户的问题，再讲传统符号，不堆砌术语。今日一签应依据提供的当天黄历条目写一句温柔签语，并点明所依据的宜忌；黄历只作为民俗参考，不据此断言个人运势。没有出生时辰时，明确只有年月日三柱，不声称得到完整八字；地点未提供时不要推定真太阳时。明确这是文化体验，不给出确定预言，不替用户作医疗、法律或投资决定。' },
        { role: 'user', content: JSON.stringify(payload) },
      ],
    }),
    signal: AbortSignal.timeout(25000),
  });
  if (!response.ok) throw new Error(`AI 服务返回 ${response.status}`);
  const result = await response.json();
  return cleanText(result?.choices?.[0]?.message?.content, 6000);
}

export function createApp() {
  const aiConfig = {
    base: process.env.AI_API_BASE_URL || '',
    key: process.env.AI_API_KEY || '',
    model: process.env.AI_MODEL || '',
  };
  const sweeper = setInterval(() => {
    const now = Date.now();
    for (const [id, session] of sessions) if (session.expires < now) sessions.delete(id);
  }, 5 * 60 * 1000);
  sweeper.unref();
  const server = createServer(async (req, res) => {
    try {
      const url = new URL(req.url, 'http://localhost');
      const remote = req.socket.remoteAddress || '';
      const localRequest = remote === '127.0.0.1' || remote === '::1' ||
        remote === '::ffff:127.0.0.1';
      if (url.pathname === '/setup' && req.method === 'GET') {
        if (!localRequest) return json(res, 403, { error: '请在运行服务的电脑上打开设置页' });
        const html = await readFile(join(root, 'setup.html'));
        res.writeHead(200, { 'content-type': 'text/html; charset=utf-8',
          'cache-control': 'no-store' });
        return res.end(html);
      }
      if (url.pathname === '/api/local-config' && req.method === 'POST') {
        if (!localRequest || req.headers['content-type'] !== 'application/json')
          return json(res, 403, { error: '仅可在本机设置' });
        const origin = req.headers.origin;
        const allowedOrigin = new URL(`http://${req.headers.host}`);
        if (origin && (origin !== allowedOrigin.origin ||
            !['localhost', '127.0.0.1'].includes(allowedOrigin.hostname)))
          return json(res, 403, { error: '页面来源不正确' });
        const input = await body(req);
        const base = cleanText(input?.base, 300);
        const model = cleanText(input?.model, 120);
        const key = cleanText(input?.key, 1000);
        let endpoint;
        try { endpoint = new URL(base); } catch { /* reported below */ }
        if (!endpoint || endpoint.protocol !== 'https:' || !model || !key)
          return json(res, 400, { error: '请填写 HTTPS 接口地址、模型名和密钥' });
        aiConfig.base = base;
        aiConfig.model = model;
        aiConfig.key = key;
        return json(res, 200, { ready: true });
      }
      const qr = /^\/q\/([A-Za-z0-9]+)$/.exec(url.pathname);
      if (req.method === 'GET' && qr) {
        const casting = parseQrPayload(qr[1]);
        const id = randomBytes(18).toString('base64url');
        sessions.set(id, { casting, expires: Date.now() + SESSION_MS, interpretations: 0 });
        res.writeHead(303, { location: `/s/${id}`, 'cache-control': 'no-store' });
        return res.end();
      }
      if (req.method === 'GET' && url.pathname === '/api/config') {
        return json(res, 200, { aiReady: Boolean(aiConfig.base && aiConfig.key && aiConfig.model) });
      }
      if (req.method === 'GET' && url.pathname === '/api/today') {
        const calendar = dailyCalendar(url.searchParams.get('date'));
        return json(res, 200, {
          date: calendar.date, lunarDate: calendar.lunarDate,
          dayType: calendar.dayType,
          yi: calendar.yi.slice(0, 2).join('·'),
          ji: calendar.ji.slice(0, 2).join('·'),
        });
      }
      if (req.method === 'POST' && url.pathname === '/api/sessions') {
        // Device authentication must be configured before Internet deployment.
        const token = process.env.DEVICE_TOKEN;
        if (!token || req.headers.authorization !== `Bearer ${token}`) {
          return json(res, 401, { error: '未授权设备' });
        }
        const casting = await body(req);
        if (!validateCasting(casting)) return json(res, 400, { error: '起卦数据无效' });
        const id = randomBytes(18).toString('base64url');
        sessions.set(id, { casting, expires: Date.now() + SESSION_MS, interpretations: 0 });
        const baseUrl = process.env.APP_BASE_URL || `http://${req.headers.host}`;
        return json(res, 201, { id, url: new URL(`/s/${id}`, baseUrl).toString(), expiresIn: 1800 });
      }
      const match = /^\/api\/sessions\/([A-Za-z0-9_-]+)(\/interpret)?$/.exec(url.pathname);
      if (match) {
        const session = getSession(match[1]);
        if (!session) return json(res, 404, { error: '本次签卦已过期，请重新开始' });
        if (req.method === 'GET' && !match[2]) {
          const calendar = session.casting.mode === 'daily'
            ? dailyCalendar(session.casting.date) : undefined;
          return json(res, 200, { casting: session.casting, calendar });
        }
        if (req.method === 'POST' && match[2]) {
          if (session.interpretations >= 3) return json(res, 429, { error: '本次签卦解读次数已用完，请重新起签' });
          const input = await body(req);
          if (!input || typeof input !== 'object') return json(res, 400, { error: '输入无效' });
          const result = await interpret(session, input, aiConfig);
          if (!result) throw new Error('AI 未返回解读');
          session.interpretations++;
          return json(res, 200, { result });
        }
      }
      if (req.method === 'GET' && (url.pathname === '/' || /^\/s\/[A-Za-z0-9_-]+$/.test(url.pathname))) {
        const html = await readFile(join(root, 'index.html'));
        res.writeHead(200, { 'content-type': 'text/html; charset=utf-8', 'cache-control': 'no-store' });
        return res.end(html);
      }
      if (req.method === 'GET' && url.pathname === '/app.js') {
        const js = await readFile(join(root, 'app.js'));
        res.writeHead(200, { 'content-type': 'text/javascript; charset=utf-8' });
        return res.end(js);
      }
      return json(res, 404, { error: '页面不存在' });
    } catch (error) {
      const message = error instanceof SyntaxError ? 'JSON 格式错误' :
        error instanceof Error ? error.message : '未知错误';
      const badInput = /日期|出生资料|超出支持范围|无效/.test(message);
      return json(res, message === '请求内容过长' ? 413 : badInput ? 400 : 500,
                  { error: message });
    }
  });
  server.on('close', () => clearInterval(sweeper));
  return server;
}

if (process.argv[1] && fileURLToPath(import.meta.url) === process.argv[1]) {
  const port = Number(process.env.PORT || 8787);
  createApp().listen(port, () => console.log(`Companion listening on ${port}`));
}
