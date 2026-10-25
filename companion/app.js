const id = location.pathname.match(/^\/s\/([A-Za-z0-9_-]+)$/)?.[1];
const names = { daily: '今日一签', meihua: '一念成卦', liuyao: '六掷问事', bazi: '生辰小笺' };
const $ = selector => document.querySelector(selector);
let casting = null;
let latest = null;
const STORAGE_KEY = 'suishen-xiaoqian-records';
for (let hour = 0; hour < 24; hour++) {
  const option = document.createElement('option');
  option.value = String(hour);
  option.textContent = `${String(hour).padStart(2, '0')}:00—${String(hour).padStart(2, '0')}:59`;
  $('#birthHour').append(option);
}

$('#calendar').addEventListener('change', () => {
  const lunar = $('#calendar').value === 'lunar';
  $('#birthDate').value = '';
  $('#birthDate').type = lunar ? 'text' : 'date';
  $('#birthDate').placeholder = lunar ? '农历 YYYY-MM-DD，例如 1990-02-30' : '';
  $('#birthDate').inputMode = lunar ? 'numeric' : '';
});

function describe(value) {
  if (value.mode === 'daily') return `日期：${value.date}。以下是当天的传统黄历参考条目。`;
  if (value.mode === 'meihua') {
    const trigrams = ['', '乾', '兑', '离', '震', '巽', '坎', '艮', '坤'];
    const time = value.localTime ? `${value.localTime.slice(0, 4)}-${value.localTime.slice(4, 6)}-${value.localTime.slice(6, 8)} ${value.localTime.slice(8, 10)}:${value.localTime.slice(10, 12)}` : '已记录';
    return `起卦时间：${time}。上卦${trigrams[value.upperTrigram] || ''}，下卦${trigrams[value.lowerTrigram] || ''}，第 ${value.movingLine} 爻有变化。`;
  }
  if (value.mode === 'liuyao') return `六次记录（自下而上）：${value.lines.map((line, i) => `第${i + 1}掷${line % 2 ? '阳' : '阴'}${line === 6 || line === 9 ? '动' : '静'}`).join(' · ')}。扫码已自动带入。`;
  return '出生资料仅用于本次排盘；填写不全时会标明可确定的部分。';
}

async function load() {
  if (!id) throw new Error('请扫描设备显示的本次二维码');
  const [response, configResponse] = await Promise.all([
    fetch(`/api/sessions/${id}`), fetch('/api/config'),
  ]);
  const data = await response.json();
  if (!response.ok) throw new Error(data.error || '无法读取本次记录');
  if (configResponse.ok && !(await configResponse.json()).aiReady) {
    $('#aiNotice').hidden = false;
    $('#aiNotice').textContent = '白话解读尚未启用：请先在运行此页面的电脑配置 AI 模型接口。';
    $('#submit').disabled = true;
  }
  casting = data.casting;
  $('#mode').textContent = names[casting.mode] || '随身小签';
  $('#casting').textContent = data.calendar
    ? `${describe(casting)}\n农历：${data.calendar.lunarDate}；${data.calendar.dayType}${data.calendar.daySpirit}。\n宜：${data.calendar.yi.join('、')}。\n忌：${data.calendar.ji.join('、')}。\n来源：${data.calendar.source}`
    : describe(casting);
  $('#questionForm').hidden = false;
  $('#birthFields').hidden = casting.mode !== 'bazi';
  if (casting.mode === 'bazi') $('#questionForm h2').textContent = '填写出生资料';
}

$('#questionForm').addEventListener('submit', async event => {
  event.preventDefault();
  $('#message').textContent = '';
  $('#submit').disabled = true;
  $('#submit').textContent = '正在解读…';
  try {
    const question = $('#question').value.trim();
    if (!question && !['daily', 'bazi'].includes(casting.mode)) throw new Error('请先写下你想问的事');
    const birth = casting.mode === 'bazi' ? {
      name: $('#birthName').value.trim(),
      date: $('#birthDate').value,
      calendar: $('#calendar').value,
      leapMonth: $('#calendar').value === 'lunar' && $('#leapMonth').checked,
      hour: $('#birthHour').value === '' ? null : Number($('#birthHour').value),
      place: $('#birthPlace').value.trim(),
    } : undefined;
    if (casting.mode === 'bazi' && !birth.date) throw new Error('请填写出生日期');
    const response = await fetch(`/api/sessions/${id}/interpret`, {
      method: 'POST', headers: { 'content-type': 'application/json' },
      body: JSON.stringify({ question, category: $('#category').value, birth }),
    });
    const data = await response.json();
    if (!response.ok) throw new Error(data.error || '解读失败');
    latest = { casting, question, category: $('#category').value, birth, result: data.result, savedAt: new Date().toISOString() };
    $('#result').textContent = data.result;
    $('#resultPanel').hidden = false;
    $('#resultPanel').scrollIntoView({ behavior: 'smooth' });
  } catch (error) {
    $('#message').textContent = error.message;
    $('#message').scrollIntoView({ behavior: 'smooth', block: 'center' });
  } finally {
    $('#submit').disabled = false;
    $('#submit').textContent = '看白话解读';
  }
});

$('#save').addEventListener('click', () => {
  if (!latest) return;
  const records = savedRecords();
  records.push(latest);
  localStorage.setItem(STORAGE_KEY, JSON.stringify(records.slice(-20)));
  $('#saveStatus').textContent = '已保存到此浏览器。';
  if (!$('#historyPanel').hidden) renderHistory();
});

$('#clear').addEventListener('click', () => {
  localStorage.removeItem(STORAGE_KEY);
  $('#saveStatus').textContent = '此浏览器保存的记录已删除。';
  renderHistory();
});

function savedRecords() {
  try {
    const value = JSON.parse(localStorage.getItem(STORAGE_KEY) || '[]');
    return Array.isArray(value) ? value.slice(-20) : [];
  } catch { return []; }
}

function renderHistory() {
  const list = $('#historyList');
  list.replaceChildren();
  const records = savedRecords().reverse();
  if (!records.length) {
    const empty = document.createElement('p');
    empty.className = 'muted';
    empty.textContent = '还没有保存过签。';
    list.append(empty);
    return;
  }
  for (const record of records) {
    const item = document.createElement('div');
    item.className = 'history-item';
    const title = document.createElement('strong');
    title.textContent = `${names[record.casting?.mode] || '旧签'} · ${new Date(record.savedAt).toLocaleString('zh-CN')}`;
    const content = document.createElement('p');
    content.textContent = `${record.question || '未填写问题'}\n\n${record.result || ''}`;
    item.append(title, content);
    list.append(item);
  }
}

$('#historyToggle').addEventListener('click', () => {
  $('#historyPanel').hidden = !$('#historyPanel').hidden;
  if (!$('#historyPanel').hidden) renderHistory();
});

load().catch(error => { $('#mode').textContent = '暂时无法打开'; $('#casting').textContent = ''; $('#loadError').textContent = error.message; });
