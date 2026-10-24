// LINE の Webhook を受け、署名を検証して返信する。許可は LineUsers、通知設定は NotifyUsers。
// 本体の操作取得と測定は /v1/ops と /v1/measurements。測定は History に足す。

const ALLOW_KEY = "allowed-user-ids";
const NOTIFY_KEY = "notify-on-user-ids";
const NOTIFY_PREF_KEY = "notify-pref-by-user";
const LAST_ALERT_PUSH_KEY = "last-alert-push-by-user";
const BOOT_NOTIFY_KEY = "boot-notify-user-ids";
const NOTIFY_PREF_STANDARD = "standard";
const NOTIFY_PREF_SAFE = "safe";
const NOTIFY_PREF_ALWAYS = "always";
const HISTORY_KEY = "measurements";
const HISTORY_LIMIT = 1008;
const DAY_MS = 24 * 60 * 60 * 1000;
const LINE_TEXT_LIMIT = 5000;
const LINE_MONTHLY_LIMIT = 200;
const DETAIL_BUBBLES = 5;
const DETAIL_OVERFLOW = "古い記録は1回の送信に収まりませんでした。";

const DANGER_LABEL = {
  NORMAL: "正常",
  WARNING: "注意",
  DANGER: "危険",
  CRITICAL: "極めて危険",
};

const TEMP_STATES = ["TEMP_FRZN", "TEMP_COLD", "TEMP_BEST", "TEMP_HIGH", "TEMP_CRIT"];
const HUM_STATES = ["HUM_DRY", "HUM_LOW", "HUM_BEST", "HUM_HIGH", "HUM_WET"];
const DANGERS = ["NORMAL", "WARNING", "DANGER", "CRITICAL"];
const TEMP_LABEL = {
  TEMP_FRZN: "低温異常",
  TEMP_COLD: "低温警告",
  TEMP_BEST: "正常",
  TEMP_HIGH: "高温警告",
  TEMP_CRIT: "高温異常",
};
const HUM_LABEL = {
  HUM_DRY: "乾燥異常",
  HUM_LOW: "乾燥警告",
  HUM_BEST: "正常",
  HUM_HIGH: "高湿度警告",
  HUM_WET: "高湿度異常",
};

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    if (url.pathname === "/v1/ops" && request.method === "GET") {
      return deviceOps(request, env, url);
    }
    if (url.pathname === "/v1/measurements" && request.method === "POST") {
      return deviceMeasurement(request, env);
    }
    if (request.method !== "POST") {
      return new Response("ok");
    }

    const body = await request.text();
    const signature = request.headers.get("x-line-signature") ?? "";
    if (!(await verifySignature(body, signature, env.CHANNEL_SECRET))) {
      return new Response("invalid signature", { status: 401 });
    }

    const payload = JSON.parse(body);
    for (const event of payload.events ?? []) {
      if (event.type !== "message" || event.message?.type !== "text" || !event.replyToken) {
        continue;
      }
      if (event.source?.type !== "user") {
        continue;
      }

      const userId = event.source.userId;
      const text = event.message.text.trim();

      if (userId === env.ADMIN_USER_ID && (text.startsWith("許可解除 ") || text.startsWith("許可 "))) {
        await reply(env.CHANNEL_ACCESS_TOKEN, event.replyToken, await updateAllowList(env.LineUsers, text));
        continue;
      }

      if (!(await isAllowed(env, userId))) {
        await reply(env.CHANNEL_ACCESS_TOKEN, event.replyToken, {
          type: "text",
          text: `未許可です。管理者に次のIDを伝えてください。\n${userId}`,
        });
        continue;
      }

      await reply(env.CHANNEL_ACCESS_TOKEN, event.replyToken, await messagesFor(env, userId, text));
    }

    return new Response("ok");
  },
};

// 本体の Bearer が Secret と一致するかを返す。
function authorized(request, env) {
  const header = request.headers.get("Authorization") ?? "";
  const prefix = "Bearer ";
  const secret = env.DEVICE_TOKEN;
  if (typeof secret !== "string" || secret.length === 0 || !header.startsWith(prefix)) {
    return false;
  }
  const got = header.slice(prefix.length);
  if (got.length !== secret.length) {
    return false;
  }
  let diff = 0;
  for (let i = 0; i < got.length; i++) {
    diff |= got.charCodeAt(i) ^ secret.charCodeAt(i);
  }
  return diff === 0;
}

// 認証が無ければ 401 を返す。
function rejectDevice(request, env) {
  if (!authorized(request, env)) {
    return new Response("unauthorized", { status: 401 });
  }
  return null;
}

// 日本時間の ISO 8601 を返す。
function japanIso(date = new Date()) {
  const parts = new Intl.DateTimeFormat("en-US", {
    timeZone: "Asia/Tokyo",
    year: "numeric",
    month: "2-digit",
    day: "2-digit",
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
    hourCycle: "h23",
  }).formatToParts(date);
  const get = (type) => parts.find((part) => part.type === type)?.value ?? "";
  return `${get("year")}-${get("month")}-${get("day")}T${get("hour")}:${get("minute")}:${get("second")}+09:00`;
}

// 起動時の操作取得。認証済み GET には常に時刻と通知モードを載せる（本体は boot 同期のときだけ呼ぶ）。
async function deviceOps(request, env, url) {
  const rejected = rejectDevice(request, env);
  if (rejected) {
    return rejected;
  }
  const notifyIds = await alwaysUserIds(env.NotifyUsers);
  const notifyMode = notifyIds.length > 0 ? 1 : 0;
  await storeBootNotify(env.NotifyUsers, notifyMode === 1 ? notifyIds : []);
  const body = {
    ops: [],
    time: japanIso(),
    notifyMode,
  };
  return Response.json(body, {
    headers: {
      "Content-Encoding": "identity",
      "Cache-Control": "no-transform",
    },
  });
}

// 起動時に ON だった利用者を、内容が変わったときだけ書く。
async function storeBootNotify(kv, ids) {
  const next = JSON.stringify(ids);
  const prev = await kv.get(BOOT_NOTIFY_KEY);
  if (prev !== next) {
    await kv.put(BOOT_NOTIFY_KEY, next);
  }
}

// 測定を履歴へ足し、notify と periodic に従って LINE へ送る。
async function deviceMeasurement(request, env) {
  const rejected = rejectDevice(request, env);
  if (rejected) {
    return rejected;
  }
  let payload;
  try {
    payload = JSON.parse(await request.text());
  } catch {
    return new Response("bad json", { status: 400 });
  }
  const row = measurementRow(payload);
  if (!row) {
    return new Response("bad measurement", { status: 400 });
  }
  await appendHistory(env.History, row.record);
  const text = lineText(row.record, row.notify, row.periodic);
  if (text) {
    try {
      if (row.notify === 1) {
        const ids = await allRecipients(env);
        await pushAlertFiltered(env, ids, row.record, text);
      } else {
        const ids = await readIdList(env.NotifyUsers, BOOT_NOTIFY_KEY);
        await pushText(env, ids, text);
      }
    } catch {
      // 履歴は残っている。LINE の失敗で本体に再送させない。
    }
  }
  return new Response("ok");
}

// 本文を履歴 1 件と通知フラグに分ける。足りなければ null。
function measurementRow(payload) {
  if (!payload || typeof payload !== "object") {
    return null;
  }
  if (!Number.isFinite(payload.temperature) || !Number.isFinite(payload.humidity)) {
    return null;
  }
  if (!TEMP_STATES.includes(payload.temperatureState) || !HUM_STATES.includes(payload.humidityState)) {
    return null;
  }
  if (!DANGERS.includes(payload.danger)) {
    return null;
  }
  if (payload.notify !== 0 && payload.notify !== 1) {
    return null;
  }
  if (payload.periodic !== 0 && payload.periodic !== 1) {
    return null;
  }
  return {
    notify: payload.notify,
    periodic: payload.periodic,
    record: {
      at: japanIso(),
      temperature: payload.temperature,
      humidity: payload.humidity,
      temperatureState: payload.temperatureState,
      humidityState: payload.humidityState,
      danger: payload.danger,
    },
  };
}

// 履歴の末尾へ足し、1008 件を超えた分は古いものから捨てる。
async function appendHistory(kv, record) {
  const records = await readHistory(kv);
  records.push(record);
  const kept = records.length > HISTORY_LIMIT ? records.slice(records.length - HISTORY_LIMIT) : records;
  await kv.put(HISTORY_KEY, JSON.stringify(kept));
}

// 送る文面を決める。notify が 1 のときは定期通知を重ねない。
function lineText(record, notify, periodic) {
  if (notify === 1) {
    return alertText(record);
  }
  if (periodic === 1) {
    return periodicText(record);
  }
  return "";
}

// 異常か復帰の文面を返す。
function alertText(row) {
  const temp = formatValue(row.temperature);
  const hum = formatValue(row.humidity);
  const danger = dangerText(row.danger);
  if (row.danger === "CRITICAL" && row.temperatureState === "TEMP_CRIT") {
    return `【高温異常】\n危険度：${danger}\n\n温度：${temp}℃\n湿度：${hum}%\n\n高温状態を検知しました。`;
  }
  if (row.danger === "CRITICAL" && row.temperatureState === "TEMP_FRZN") {
    return `【低温異常】\n危険度：${danger}\n\n温度：${temp}℃\n湿度：${hum}%\n\n低温状態を検知しました。`;
  }
  if (row.danger === "CRITICAL" && row.humidityState === "HUM_DRY") {
    return `【湿度異常】\n危険度：${danger}\n\n温度：${temp}℃\n湿度：${hum}%\n\n乾燥状態を検知しました。`;
  }
  if (row.danger === "CRITICAL") {
    return `【湿度異常】\n危険度：${danger}\n\n温度：${temp}℃\n湿度：${hum}%\n\n高湿度状態を検知しました。`;
  }
  if (row.danger === "NORMAL") {
    return `【正常復帰】\n環境危険度：${danger}\n\n温度：${temp}℃\n湿度：${hum}%\n\n温湿度が正常範囲に戻りました。`;
  }
  const state = row.danger === "DANGER" ? "温度・湿度警告" : statePhrase(row);
  return `【環境異常】\n危険度：${danger}\n状態：${state}\n\n温度：${temp}℃\n湿度：${hum}%`;
}

// 警告の状態名を返す。
function statePhrase(row) {
  const parts = [];
  if (row.temperatureState !== "TEMP_BEST") {
    parts.push(TEMP_LABEL[row.temperatureState]);
  }
  if (row.humidityState !== "HUM_BEST") {
    parts.push(HUM_LABEL[row.humidityState]);
  }
  return parts.length > 0 ? parts.join("・") : "正常";
}

// 定期通知の文面を返す。
function periodicText(row) {
  return `【定期通知】\n環境危険度：${dangerText(row.danger)}\n\nシステムは正常に動作しています。\n\n温度：${formatValue(row.temperature)}℃\n湿度：${formatValue(row.humidity)}%`;
}

// 許可済みユーザーと管理者へ同じ文面を送る。
async function allRecipients(env) {
  const ids = await readIdList(env.LineUsers, ALLOW_KEY);
  if (typeof env.ADMIN_USER_ID === "string" && env.ADMIN_USER_ID && !ids.includes(env.ADMIN_USER_ID)) {
    ids.push(env.ADMIN_USER_ID);
  }
  return ids;
}

// 通常通知モードの利用者だけ異常 push を省略する。
async function pushAlertFiltered(env, ids, record, text) {
  if (!ids || ids.length === 0) {
    return;
  }
  const lastMap = await readLastAlertPushes(env.NotifyUsers);
  const toSend = [];
  const nextLast = { ...lastMap };
  for (const userId of ids) {
    const pref = await getUserNotifyPref(env.NotifyUsers, userId);
    if (pref === NOTIFY_PREF_STANDARD) {
      const last = lastMap[userId];
      if (!shouldPushAlert(record, last)) {
        continue;
      }
    }
    toSend.push(userId);
    nextLast[userId] = alertPushSnapshot(record);
  }
  if (toSend.length === 0) {
    return;
  }
  await pushText(env, toSend, text);
  await writeLastAlertPushes(env.NotifyUsers, nextLast);
}

// 異常 push の省略判定。lastSent が無いときは送る。
function shouldPushAlert(record, lastSent) {
  if (!lastSent) {
    return true;
  }
  if (record.danger === "NORMAL") {
    return true;
  }
  if (record.temperatureState !== lastSent.temperatureState) {
    return true;
  }
  if (record.humidityState !== lastSent.humidityState) {
    return true;
  }
  const rankNow = dangerRank(record.danger);
  const rankLast = dangerRank(lastSent.danger);
  if (rankNow > rankLast) {
    return false;
  }
  if (record.danger === lastSent.danger) {
    return false;
  }
  return true;
}

// 省略比較用の危険度順。
function dangerRank(danger) {
  const index = DANGERS.indexOf(danger);
  return index >= 0 ? index : 0;
}

// 最後に送った異常の状態。
function alertPushSnapshot(record) {
  return {
    danger: record.danger,
    temperatureState: record.temperatureState,
    humidityState: record.humidityState,
  };
}

// 当月の push 通数を LINE から取得する。失敗時は null。
async function fetchMonthlyUsage(token) {
  if (typeof token !== "string" || token.length === 0) {
    return null;
  }
  try {
    const res = await fetch("https://api.line.me/v2/bot/message/quota/consumption", {
      headers: { Authorization: `Bearer ${token}` },
    });
    if (!res.ok) {
      return null;
    }
    const data = await res.json();
    if (!Number.isFinite(data.totalUsage) || data.totalUsage < 0) {
      return null;
    }
    return data.totalUsage;
  } catch {
    return null;
  }
}

// push 本文の末尾に当月通数行を付ける。5000 文字を超えるときは付けない。
function appendQuotaSuffix(text, used, limit) {
  const suffix = `\n\n(今月 ${used}/${limit})`;
  const combined = text + suffix;
  if (combined.length > LINE_TEXT_LIMIT) {
    return text;
  }
  return combined;
}

// 同じ文面を利用者へ push する。
async function pushText(env, ids, text) {
  if (!ids || ids.length === 0) {
    return;
  }
  const token = env.CHANNEL_ACCESS_TOKEN;
  let bodyText = text;
  const usage = await fetchMonthlyUsage(token);
  if (usage !== null) {
    bodyText = appendQuotaSuffix(text, usage + ids.length, LINE_MONTHLY_LIMIT);
  }
  await fetch("https://api.line.me/v2/bot/message/multicast", {
    method: "POST",
    headers: {
      Authorization: `Bearer ${token}`,
      "Content-Type": "application/json",
    },
    body: JSON.stringify({
      to: ids.slice(0, 500),
      messages: [{ type: "text", text: bodyText }],
    }),
  });
}

// チャネルシークレットで Webhook の署名を検証する。
async function verifySignature(body, signature, secret) {
  const key = await crypto.subtle.importKey(
    "raw",
    new TextEncoder().encode(secret),
    { name: "HMAC", hash: "SHA-256" },
    false,
    ["sign"],
  );
  const mac = await crypto.subtle.sign("HMAC", key, new TextEncoder().encode(body));
  const bytes = new Uint8Array(mac);
  let binary = "";
  for (const b of bytes) {
    binary += String.fromCharCode(b);
  }
  return btoa(binary) === signature;
}

// 許可済みの文字に対する返信を返す。詳細だけ複数の吹き出しになる。
async function messagesFor(env, userId, text) {
  if (text === "その他") {
    return otherMenu();
  }
  if (text === "いまの状態") {
    return currentStateMessage(env, userId);
  }
  if (text === "通知設定" || text === "定期通知") {
    return notifyPrefStatusMessage(env, userId);
  }
  const prefFromText = notifyPrefFromMessage(text);
  if (prefFromText) {
    return setNotifyPref(env.NotifyUsers, userId, prefFromText);
  }
  if (text === "オン") {
    return setNotifyPref(env.NotifyUsers, userId, NOTIFY_PREF_ALWAYS);
  }
  if (text === "オフ") {
    return setNotifyPref(env.NotifyUsers, userId, NOTIFY_PREF_SAFE);
  }
  if (text === "平均") {
    return averageMessage(await recordsWithin24h(env.History));
  }
  if (text === "詳細") {
    return detailMessages(await recordsWithin24h(env.History));
  }
  return { type: "text", text: "操作を選んでください。" };
}

// 平均と詳細のボタンを添えて案内する。
function otherMenu() {
  return {
    type: "text",
    text: "操作を選んでください。",
    quickReply: {
      items: [
        { type: "action", action: { type: "message", label: "平均", text: "平均" } },
        { type: "action", action: { type: "message", label: "詳細", text: "詳細" } },
      ],
    },
  };
}

// 履歴の最新1件を返す。無ければ温度と湿度は出さない。
async function currentStateMessage(env, userId) {
  const records = await readHistory(env.History);
  const notifyLine = `通知設定：${notifyPrefLabel(await getUserNotifyPref(env.NotifyUsers, userId))}`;
  const latest = records.at(-1);
  if (!latest) {
    return { type: "text", text: `【いまの状態】\n未取得\n${notifyLine}` };
  }
  return {
    type: "text",
    text: [
      "【いまの状態】",
      `危険度：${dangerText(latest.danger)}`,
      "",
      `温度：${formatValue(latest.temperature)}℃`,
      `湿度：${formatValue(latest.humidity)}%`,
      "",
      `温度の状態：${TEMP_LABEL[latest.temperatureState] ?? ""}`,
      `湿度の状態：${HUM_LABEL[latest.humidityState] ?? ""}`,
      `日時：${formatJapanTime(latest.at)}`,
      notifyLine,
    ].join("\n"),
  };
}

// 通知設定の現モードと切替ボタンを返す。
async function notifyPrefStatusMessage(env, userId) {
  const pref = await getUserNotifyPref(env.NotifyUsers, userId);
  return {
    type: "text",
    text: [
      `通知設定：${notifyPrefLabel(pref)}`,
      "",
      "通常通知 … 定期通知なし。状態が変わらない再通知・危険度だけの上昇は省略",
      "安心 … 定期通知なし。異常・復帰はすべて通知",
      "常に通知 … 60分ごとの定期通知と、異常のたびに通知",
      "",
      "本体をリセットすると、定期通知の開始・停止が反映されます。",
    ].join("\n"),
    quickReply: notifyPrefQuickReply(),
  };
}

// 通知設定の3モードボタン。並びは通常通知、安心、常に通知。
function notifyPrefQuickReply() {
  return {
    items: [
      { type: "action", action: { type: "message", label: "通常通知", text: "通常通知モード" } },
      { type: "action", action: { type: "message", label: "安心", text: "安心モード" } },
      { type: "action", action: { type: "message", label: "常に通知", text: "常に通知モード" } },
    ],
  };
}

// メッセージから通知設定の内部値を返す。該当しなければ null。
function notifyPrefFromMessage(text) {
  if (text === "通常通知モード") {
    return NOTIFY_PREF_STANDARD;
  }
  if (text === "安心モード" || text === "通常モード") {
    return NOTIFY_PREF_SAFE;
  }
  if (text === "簡易通知モード") {
    return NOTIFY_PREF_STANDARD;
  }
  if (text === "常に通知モード") {
    return NOTIFY_PREF_ALWAYS;
  }
  return null;
}

// 通知設定を1件だけ書く。
async function setNotifyPref(kv, userId, mode) {
  const prefs = await readNotifyPrefs(kv);
  prefs[userId] = mode;
  await writeNotifyPrefs(kv, prefs);
  if (mode === NOTIFY_PREF_STANDARD) {
    await clearLastAlertPush(kv, userId);
  }
  return {
    type: "text",
    text: [
      `通知設定：${notifyPrefLabel(mode)}`,
      "",
      "本体をリセットすると、定期通知の開始・停止が反映されます。",
    ].join("\n"),
  };
}

// 利用者ごとの通知設定を読む。無ければ安心。
async function getUserNotifyPref(kv, userId) {
  const prefs = await readNotifyPrefs(kv);
  return prefs[userId] ?? NOTIFY_PREF_SAFE;
}

// 常に通知モードの userId 一覧を返す。
async function alwaysUserIds(kv) {
  const prefs = await readNotifyPrefs(kv);
  return Object.keys(prefs).filter((id) => prefs[id] === NOTIFY_PREF_ALWAYS);
}

// 通知設定の JSON を読む。旧リストがあれば always へ移す。
async function readNotifyPrefs(kv) {
  const raw = await kv.get(NOTIFY_PREF_KEY);
  if (raw) {
    try {
      const parsed = JSON.parse(raw);
      if (parsed && typeof parsed === "object" && !Array.isArray(parsed)) {
        return sanitizeNotifyPrefs(parsed);
      }
    } catch {
      // 壊れていれば旧形式へフォールバックする。
    }
  }
  const legacy = await readIdList(kv, NOTIFY_KEY);
  const prefs = {};
  for (const id of legacy) {
    prefs[id] = NOTIFY_PREF_ALWAYS;
  }
  return prefs;
}

// 通知設定の値だけ残す。
function sanitizeNotifyPrefs(obj) {
  const allowed = new Set([NOTIFY_PREF_STANDARD, NOTIFY_PREF_SAFE, NOTIFY_PREF_ALWAYS]);
  const out = {};
  for (const [userId, mode] of Object.entries(obj)) {
    if (typeof userId === "string" && allowed.has(mode)) {
      out[userId] = mode;
    }
  }
  return out;
}

// 通知設定を KV に書く。
async function writeNotifyPrefs(kv, prefs) {
  await kv.put(NOTIFY_PREF_KEY, JSON.stringify(sanitizeNotifyPrefs(prefs)));
}

// 表示名を返す。
function notifyPrefLabel(mode) {
  if (mode === NOTIFY_PREF_STANDARD) {
    return "通常通知モード";
  }
  if (mode === NOTIFY_PREF_ALWAYS) {
    return "常に通知モード";
  }
  return "安心モード";
}

// 通常通知用の最後に送った状態を読む。
async function readLastAlertPushes(kv) {
  const raw = await kv.get(LAST_ALERT_PUSH_KEY);
  if (!raw) {
    return {};
  }
  try {
    const parsed = JSON.parse(raw);
    if (parsed && typeof parsed === "object" && !Array.isArray(parsed)) {
      return parsed;
    }
  } catch {
    return {};
  }
  return {};
}

// 通常通知用の最後に送った状態を書く。
async function writeLastAlertPushes(kv, map) {
  await kv.put(LAST_ALERT_PUSH_KEY, JSON.stringify(map));
}

// 利用者の省略比較用スナップショットを消す。
async function clearLastAlertPush(kv, userId) {
  const map = await readLastAlertPushes(kv);
  if (map[userId] === undefined) {
    return;
  }
  delete map[userId];
  await writeLastAlertPushes(kv, map);
}

// 直近24時間の平均を返す。欠測は件数に入れない。
function averageMessage(records) {
  if (records.length === 0) {
    return { type: "text", text: "直近24時間の記録はありません。" };
  }
  const temperature = records.reduce((sum, row) => sum + row.temperature, 0) / records.length;
  const humidity = records.reduce((sum, row) => sum + row.humidity, 0) / records.length;
  return {
    type: "text",
    text: [
      "【平均】",
      "対象期間：直近24時間",
      `集計時間範囲：${formatJapanTime(records[0].at)} 〜 ${formatJapanTime(records.at(-1).at)}`,
      `記録数：${records.length}`,
      `平均温度：${formatValue(temperature)}℃`,
      `平均湿度：${formatValue(humidity)}%`,
    ].join("\n"),
  };
}

// 直近24時間の記録を、吹き出し5つまでの1回の返信に収める。
function detailMessages(records) {
  if (records.length === 0) {
    return [{ type: "text", text: "直近24時間の記録はありません。" }];
  }

  let blocks = records.map(recordBlock);
  let overflow = false;
  while (packDetail(blocks, overflow).length > DETAIL_BUBBLES) {
    blocks = blocks.slice(1);
    overflow = true;
  }

  return packDetail(blocks, overflow).map((text) => ({ type: "text", text }));
}

// 1件の記録を詳細の1塊にする。
function recordBlock(row) {
  return [
    formatJapanTime(row.at),
    `危険度：${dangerText(row.danger)}`,
    `温度：${formatValue(row.temperature)}℃`,
    `湿度：${formatValue(row.humidity)}%`,
    `温度の状態：${TEMP_LABEL[row.temperatureState] ?? ""}`,
    `湿度の状態：${HUM_LABEL[row.humidityState] ?? ""}`,
  ].join("\n");
}

// 詳細の本文を、1吹き出し5000文字以内で分割する。
function packDetail(blocks, overflow) {
  const chunks = [];
  let current = overflow ? `【詳細】\n${DETAIL_OVERFLOW}` : "【詳細】";
  for (const block of blocks) {
    const next = `${current}\n\n${block}`;
    if (next.length <= LINE_TEXT_LIMIT) {
      current = next;
      continue;
    }
    chunks.push(current);
    current = block;
  }
  if (current !== "") {
    chunks.push(current);
  }
  return chunks;
}

// replyToken で LINE へ返信する。messages は1件でも配列でも受ける。
async function reply(token, replyToken, messages) {
  const list = Array.isArray(messages) ? messages : [messages];
  await fetch("https://api.line.me/v2/bot/message/reply", {
    method: "POST",
    headers: {
      Authorization: `Bearer ${token}`,
      "Content-Type": "application/json",
    },
    body: JSON.stringify({
      replyToken,
      messages: list,
    }),
  });
}

// 管理者か、許可リストにいるユーザーかを返す。
async function isAllowed(env, userId) {
  if (userId === env.ADMIN_USER_ID) {
    return true;
  }
  const ids = await readIdList(env.LineUsers, ALLOW_KEY);
  return ids.includes(userId);
}

// ユーザー ID の JSON 配列を読む。
async function readIdList(kv, key) {
  const raw = await kv.get(key);
  if (!raw) {
    return [];
  }
  const ids = JSON.parse(raw);
  return Array.isArray(ids) ? ids.filter((id) => typeof id === "string") : [];
}

// 許可リストへユーザーを追加するか、外す。
async function updateAllowList(kv, text) {
  const [command, userId] = text.trim().split(/\s+/);
  if (!/^U[0-9a-f]{32}$/i.test(userId ?? "")) {
    return { type: "text", text: "ユーザーIDの形式が違います。" };
  }

  const current = await readIdList(kv, ALLOW_KEY);
  const next = command === "許可解除"
    ? current.filter((id) => id !== userId)
    : current.includes(userId) ? current : [...current, userId];
  await kv.put(ALLOW_KEY, JSON.stringify(next));
  return { type: "text", text: command === "許可解除" ? "許可を解除しました。" : "許可しました。" };
}

// History の measurements を時刻順に読む。1件は湿度、温度、湿度の状態、温度の状態、危険度、日本時間。
async function readHistory(kv) {
  const raw = await kv.get(HISTORY_KEY);
  if (!raw) {
    return [];
  }
  const rows = JSON.parse(raw);
  if (!Array.isArray(rows)) {
    return [];
  }
  return rows
    .filter((row) => Number.isFinite(recordTime(row)))
    .sort((a, b) => recordTime(a) - recordTime(b));
}

// 直近24時間に届いた記録を時刻順に返す。
async function recordsWithin24h(kv) {
  const now = Date.now();
  const from = now - DAY_MS;
  const records = await readHistory(kv);
  return records.filter((row) => {
    const time = recordTime(row);
    return time >= from && time <= now;
  });
}

// 記録の時刻をミリ秒で返す。
function recordTime(row) {
  if (!row || typeof row.temperature !== "number" || typeof row.humidity !== "number") {
    return NaN;
  }
  return parseAt(row.at);
}

// 日本時間の文字列をミリ秒にする。空白区切りも受ける。
function parseAt(at) {
  if (typeof at !== "string") {
    return NaN;
  }
  if (/^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}$/.test(at)) {
    return Date.parse(`${at.replace(" ", "T")}+09:00`);
  }
  return Date.parse(at);
}

// 危険度の日本語名を返す。
function dangerText(danger) {
  return DANGER_LABEL[danger] ?? String(danger ?? "");
}

// 温度と湿度を小数1桁にする。
function formatValue(value) {
  const tenths = Math.round(Number((Number(value) * 10).toFixed(8)));
  return (tenths / 10).toFixed(1);
}

// 日本時間の「YYYY-MM-DD HH:mm:ss」にする。
function formatJapanTime(at) {
  const time = parseAt(at);
  if (!Number.isFinite(time)) {
    return at;
  }
  const parts = new Intl.DateTimeFormat("en-US", {
    timeZone: "Asia/Tokyo",
    year: "numeric",
    month: "2-digit",
    day: "2-digit",
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
    hourCycle: "h23",
  }).formatToParts(new Date(time));
  const get = (type) => parts.find((part) => part.type === type)?.value ?? "";
  return `${get("year")}-${get("month")}-${get("day")} ${get("hour")}:${get("minute")}:${get("second")}`;
}
