import lunarLibrary from 'lunar-javascript';

const { Solar, Lunar } = lunarLibrary;

function parseYmd(value, lunarInput = false) {
  if (typeof value !== 'string' || !/^\d{4}-\d{2}-\d{2}$/.test(value)) {
    throw new Error('日期格式应为 YYYY-MM-DD');
  }
  const [year, month, day] = value.split('-').map(Number);
  if (year < 1901 || year > 2100 || month < 1 || month > 12 || day < 1 || day > 31) {
    throw new Error('日期超出支持范围');
  }
  if (!lunarInput) {
    const checked = new Date(Date.UTC(year, month - 1, day));
    if (checked.getUTCFullYear() !== year || checked.getUTCMonth() !== month - 1 ||
        checked.getUTCDate() !== day) throw new Error('无效日期');
  }
  return { year, month, day };
}

export function dailyCalendar(date) {
  const { year, month, day } = parseYmd(date);
  const solar = Solar.fromYmd(year, month, day);
  if (solar.toYmd() !== date) throw new Error('无效日期');
  const lunar = solar.getLunar();
  return {
    date,
    lunarDate: lunar.toString(),
    dayGanzhi: lunar.getDayInGanZhi(),
    dayType: lunar.getDayTianShenType(),
    daySpirit: lunar.getDayTianShen(),
    yi: lunar.getDayYi().slice(0, 8),
    ji: lunar.getDayJi().slice(0, 8),
    source: 'lunar-javascript 1.7.7；传统黄历参考，非客观预测',
  };
}

// 梅花易数时间起卦：农历月日、年支与时支参与计算；初爻为 bit 0。
export function meihuaAt(localTime) {
  if (typeof localTime !== 'string' || !/^\d{12}$/.test(localTime)) throw new Error('无效起卦时间');
  const year = Number(localTime.slice(0, 4));
  const month = Number(localTime.slice(4, 6));
  const day = Number(localTime.slice(6, 8));
  const hour = Number(localTime.slice(8, 10));
  const minute = Number(localTime.slice(10, 12));
  parseYmd(`${localTime.slice(0, 4)}-${localTime.slice(4, 6)}-${localTime.slice(6, 8)}`);
  if (hour > 23 || minute > 59) throw new Error('无效起卦时间');
  const lunar = Solar.fromYmdHms(year, month, day, hour, minute, 0).getLunar();
  const yearBranch = lunar.getYearZhiIndex() + 1; // 子=1
  const hourBranch = Math.floor(((hour + 1) % 24) / 2) + 1;
  const lunarMonth = Math.abs(lunar.getMonth());
  const lunarDay = lunar.getDay();
  const first = yearBranch + lunarMonth + lunarDay;
  const total = first + hourBranch;
  const upper = (first - 1) % 8 + 1;
  const lower = (total - 1) % 8 + 1;
  const movingLine = (total - 1) % 6 + 1;
  const trigrams = [0, 7, 3, 5, 1, 6, 2, 4, 0];
  const baseLines = trigrams[lower] | (trigrams[upper] << 3);
  return { mode: 'meihua', localTime, lunarMonth, lunarDay, yearBranch, hourBranch,
    upperTrigram: upper, lowerTrigram: lower, movingLine, baseLines,
    changedLines: baseLines ^ (1 << (movingLine - 1)) };
}

export function birthPillars(input) {
  if (!input || typeof input !== 'object') throw new Error('缺少出生资料');
  const calendar = input.calendar === 'lunar' ? 'lunar' : 'solar';
  const { year, month, day } = parseYmd(input.date, calendar === 'lunar');
  const hourKnown = Number.isInteger(input.hour) && input.hour >= 0 && input.hour <= 23;
  const minute = Number.isInteger(input.minute) && input.minute >= 0 && input.minute <= 59 ? input.minute : 0;
  const solar = calendar === 'lunar'
    ? Lunar.fromYmdHms(year, input.leapMonth ? -month : month, day,
                       hourKnown ? input.hour : 12, minute, 0).getSolar()
    : Solar.fromYmdHms(year, month, day, hourKnown ? input.hour : 12, minute, 0);
  if (calendar === 'lunar') {
    const checked = solar.getLunar();
    if (checked.getYear() !== year || checked.getMonth() !== (input.leapMonth ? -month : month) ||
        checked.getDay() !== day) throw new Error('无效农历日期');
  }
  const eight = solar.getLunar().getEightChar();
  return {
    name: typeof input.name === 'string' ? input.name.trim().slice(0, 30) : '',
    inputDate: input.date,
    inputCalendar: calendar,
    solarDate: solar.toYmd(),
    year: eight.getYear(),
    month: eight.getMonth(),
    day: eight.getDay(),
    hour: hourKnown ? eight.getTime() : null,
    hourKnown,
    place: typeof input.place === 'string' ? input.place.trim().slice(0, 80) : '',
    timeRule: '按填写的当地钟表时间排盘；第一版未作真太阳时换算。时辰不详时年月日柱仅供参考，节气交界日尤需补充出生时间。',
    source: 'lunar-javascript 1.7.7',
  };
}
