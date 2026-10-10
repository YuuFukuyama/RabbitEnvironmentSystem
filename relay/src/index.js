// LINE の Webhook を受け、署名を検証して返信する。許可は LineUsers、定期通知の ON は NotifyUsers。測定の表示は History を読む。

const ALLOW_KEY = "allowed-user-ids";
const NOTIFY_KEY = "notify-on-user-ids";
const HISTORY_KEY = "measurements";
const DAY_MS = 24 * 60 * 60 * 1000;
const LINE_TEXT_LIMIT = 5000;
const DETAIL_BUBBLES = 5;
const DETAIL_OVERFLOW = "古い記録は1回の送信に収まりませんでした。";

const DANGER_LABEL = {
  NORMAL: "正常",
  WARNING: "注意",
  DANGER: "危険",
  CRITICAL: "極めて危険",
};

export default {
  async fetch(request, env) {
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
  if (text === "定期通知") {
    return notifyStatusMessage(env, userId);
  }
  if (text === "オン" || text === "オフ") {
    return setNotify(env.NotifyUsers, userId, text === "オン");
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
  const notifyLine = `定期通知：${notifyWord(await isNotifyOn(env.NotifyUsers, userId))}`;
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
      `温度の状態：${latest.temperatureState ?? ""}`,
      `湿度の状態：${latest.humidityState ?? ""}`,
      `日時：${formatJapanTime(latest.at)}`,
      notifyLine,
    ].join("\n"),
  };
}

// その利用者の定期通知がオンかを返し、切り替えボタンを添える。
async function notifyStatusMessage(env, userId) {
  const on = await isNotifyOn(env.NotifyUsers, userId);
  return {
    type: "text",
    text: `定期通知：${notifyWord(on)}\n本体をリセットすると反映されます。`,
    quickReply: notifyQuickReply(),
  };
}

// オンまたはオフのボタンを返す。
function notifyQuickReply() {
  return {
    items: [
      { type: "action", action: { type: "message", label: "オン", text: "オン" } },
      { type: "action", action: { type: "message", label: "オフ", text: "オフ" } },
    ],
  };
}

// 定期通知の ON リストへ利用者を足すか、外す。
async function setNotify(kv, userId, on) {
  const current = await readIdList(kv, NOTIFY_KEY);
  const next = on
    ? (current.includes(userId) ? current : [...current, userId])
    : current.filter((id) => id !== userId);
  await kv.put(NOTIFY_KEY, JSON.stringify(next));
  return {
    type: "text",
    text: `定期通知を${notifyWord(on)}にしました。\n本体をリセットすると反映されます。`,
  };
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
    `温度の状態：${row.temperatureState ?? ""}`,
    `湿度の状態：${row.humidityState ?? ""}`,
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

// 定期通知が ON の利用者かを返す。
async function isNotifyOn(kv, userId) {
  const ids = await readIdList(kv, NOTIFY_KEY);
  return ids.includes(userId);
}

// オンまたはオフの表示を返す。
function notifyWord(on) {
  return on ? "オン" : "オフ";
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

// 危険度を仕様の括弧付きで返す。
function dangerText(danger) {
  const label = DANGER_LABEL[danger];
  return label ? `${danger}（${label}）` : String(danger ?? "");
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
