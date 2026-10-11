/* 温湿度の個別判定、総合危険度、通知要否を更新する。 */

#include "env_judge.h"
#include "env_config.h"

/* 危険度、検知回数、通知段階、最後に成功した測定。 */
typedef struct
{
  TempState temp_state;             /* ヒステリシス適用後の温度帯。 */
  HumState hum_state;               /* ヒステリシス適用後の湿度帯。 */
  EnvRisk risk;                     /* 低下を抑えたあとの総合危険度。 */
  SystemStatus system;              /* 環境危険度とは別の稼働状態。 */
  uint8_t warn_count;               /* WARNING 検知回数。 */
  uint8_t normal_streak;            /* 正常の連続回数。4 回で検知回数を 0 にする。 */
  uint8_t has_value;                /* 正常取得が一度でもあれば 1。 */
  uint8_t warning_notified;         /* WARNING の初回通知を出したら 1。 */
  uint8_t warning_repeated;         /* WARNING の再通知を出したら 1。 */
  uint8_t alert_sent;               /* DANGER か CRITICAL の送信に成功したら 1。復帰通知に使う。 */
  int16_t last_temp;                /* 最後に成功した温度。0.1℃単位。 */
  int16_t last_hum;                 /* 最後に成功した湿度。0.1%単位。 */
  uint32_t last_ok_sec;             /* 最後に成功した秒。センサー異常の経過分に使う。 */
  uint32_t episode_start_sec;       /* WARNING 以上になった秒。LCD の継続分の起点。 */
  uint32_t warning_since_sec;       /* 今回の WARNING 開始秒。DANGER への昇格に使う。 */
  uint32_t warning_notify_sec;      /* WARNING 初回通知の秒。再通知の起点。 */
  uint32_t danger_since_sec;        /* DANGER 開始秒。再通知と CRITICAL への昇格に使う。 */
  uint32_t danger_notify_ordinal;   /* DANGER の通知回数。次の再通知までの間隔に使う。 */
  uint32_t heartbeat_period;        /* 最後に生存確認を出した周期番号。 */
} EnvJudgeState;

/* 起動時に誰か定期通知が ON なら 1。未取得のあいだは 0。 */
static uint8_t notify_mode_on;
/* この周期の DANGER か CRITICAL を、送信成功まで復帰の資格にしない。 */
static uint8_t pending_alert;
/* この周期の WARNING 初回。失敗したら次の周期で出し直す。 */
static uint8_t pending_warn_first;
/* DANGER の遷移通知を届けたら 1。失敗しているあいだは出し直す。 */
static uint8_t danger_alert_done;
/* この周期で DANGER の遷移通知を載せた。 */
static uint8_t pending_danger_open;

/* 判定と通知の継続状態。電源が入るたびに初期値へ戻る。 */
static EnvJudgeState state =
{
  .temp_state = TEMP_BEST,            /* 警告帯には入っていない。 */
  .hum_state = HUM_BEST,              /* 警告帯には入っていない。 */
  .risk = ENV_RISK_NORMAL,            /* 総合危険度は正常から始める。 */
  .system = SYS_NORMAL,               /* 稼働状態は正常。 */
  .warn_count = 0U,                   /* 検知回数はまだない。 */
  .normal_streak = 0U,                /* 正常の連続はまだない。 */
  .has_value = 0U,                    /* 正常取得はまだない。 */
  .warning_notified = 0U,             /* WARNING の初回通知はまだ。 */
  .warning_repeated = 0U,             /* WARNING の再通知はまだ。 */
  .alert_sent = 0U,                   /* DANGER と CRITICAL の通知はまだない。 */
  .last_temp = 0,                     /* 成功した温度はまだない。 */
  .last_hum = 0,                      /* 成功した湿度はまだない。 */
  .last_ok_sec = 0U,                  /* 成功した時刻はまだない。 */
  .episode_start_sec = 0U,            /* 異常の継続はまだ始まっていない。 */
  .warning_since_sec = 0U,            /* WARNING はまだ始まっていない。 */
  .warning_notify_sec = 0U,           /* WARNING の初回通知時刻はまだない。 */
  .danger_since_sec = 0U,             /* DANGER はまだ始まっていない。 */
  .danger_notify_ordinal = 0U,        /* DANGER の通知回数は 0。 */
  .heartbeat_period = 0U              /* 生存確認はまだ出していない。 */
};

/* 経過秒を整数分にし、99 分で打ち切る。 */
static uint8_t elapsed_minutes(uint32_t now_sec, uint32_t start_sec)
{
  uint32_t minutes;

  minutes = (now_sec - start_sec) / 60U;
  if (minutes > 99U)
  {
    minutes = 99U;
  }
  return (uint8_t)minutes;
}

/* 通知を 1 件追加する。枠が無いときは 0。 */
static uint8_t push_note(WifiNote *notes, uint8_t *note_count, uint8_t note_cap,
                         WifiNoteKind kind, EnvRisk risk)
{
  WifiNote *note;

  if ((notes == NULL) || (note_count == NULL) || (*note_count >= note_cap))
  {
    return 0U;
  }

  note = &notes[*note_count];
  note->kind = kind;
  note->risk = risk;
  note->temp_state = state.temp_state;
  note->hum_state = state.hum_state;
  note->temp_deci_c = state.last_temp;
  note->hum_deci_rh = state.last_hum;
  (*note_count)++;
  return 1U;
}

/* 生存確認の周期に入っていれば通知を足す。 */
static void consider_heartbeat(uint32_t now_sec, WifiNote *notes, uint8_t *note_count,
                               uint8_t note_cap)
{
  uint32_t period;

  if ((notify_mode_on == 0U) || (HEARTBEAT_ENABLE == 0) || (HEARTBEAT_SEC == 0U))
  {
    return;
  }

  period = now_sec / HEARTBEAT_SEC;
  if (period > state.heartbeat_period)
  {
    state.heartbeat_period = period;
    push_note(notes, note_count, note_cap, WIFI_NOTE_HEARTBEAT, state.risk);
  }
}

/* ヒステリシスなしで温度帯を決める。 */
static TempState classify_temp_raw(int16_t temp_deci_c)
{
  if (temp_deci_c <= TEMP_LOW_CRITICAL_DECI)
  {
    return TEMP_FRZN;
  }
  if (temp_deci_c < TEMP_LOW_WARNING_DECI)
  {
    return TEMP_COLD;
  }
  if (temp_deci_c < TEMP_HIGH_WARNING_DECI)
  {
    return TEMP_BEST;
  }
  if (temp_deci_c < TEMP_HIGH_CRITICAL_DECI)
  {
    return TEMP_HIGH;
  }
  return TEMP_CRIT;
}

/* WARNING 中だけ解除閾値を使う。CRITICAL 帯へ入ったらすぐ移す。 */
static TempState classify_temp(int16_t temp_deci_c)
{
  if (state.temp_state == TEMP_COLD)
  {
    if (temp_deci_c <= TEMP_LOW_CRITICAL_DECI)
    {
      return TEMP_FRZN;
    }
    if (temp_deci_c < TEMP_LOW_CLEAR_DECI)
    {
      return TEMP_COLD;
    }
  }
  else if (state.temp_state == TEMP_HIGH)
  {
    if (temp_deci_c >= TEMP_HIGH_CRITICAL_DECI)
    {
      return TEMP_CRIT;
    }
    if (temp_deci_c >= TEMP_HIGH_CLEAR_DECI)
    {
      return TEMP_HIGH;
    }
  }
  return classify_temp_raw(temp_deci_c);
}

/* ヒステリシスなしで湿度帯を決める。 */
static HumState classify_hum_raw(int16_t hum_deci_rh)
{
  if (hum_deci_rh <= HUM_LOW_CRITICAL_DECI)
  {
    return HUM_DRY;
  }
  if (hum_deci_rh < HUM_LOW_WARNING_DECI)
  {
    return HUM_LOW;
  }
  if (hum_deci_rh < HUM_HIGH_WARNING_DECI)
  {
    return HUM_BEST;
  }
  if (hum_deci_rh < HUM_HIGH_CRITICAL_DECI)
  {
    return HUM_HIGH;
  }
  return HUM_WET;
}

/* WARNING 中だけ湿度の解除閾値を使う。 */
static HumState classify_hum(int16_t hum_deci_rh)
{
  if (state.hum_state == HUM_LOW)
  {
    if (hum_deci_rh <= HUM_LOW_CRITICAL_DECI)
    {
      return HUM_DRY;
    }
    if (hum_deci_rh < HUM_LOW_CLEAR_DECI)
    {
      return HUM_LOW;
    }
  }
  else if (state.hum_state == HUM_HIGH)
  {
    if (hum_deci_rh >= HUM_HIGH_CRITICAL_DECI)
    {
      return HUM_WET;
    }
    if (hum_deci_rh >= HUM_HIGH_CLEAR_DECI)
    {
      return HUM_HIGH;
    }
  }
  return classify_hum_raw(hum_deci_rh);
}

/* 温度帯を個別危険度にする。 */
static EnvRisk temp_to_risk(TempState temp_state)
{
  if ((temp_state == TEMP_FRZN) || (temp_state == TEMP_CRIT))
  {
    return ENV_RISK_CRITICAL;
  }
  if ((temp_state == TEMP_COLD) || (temp_state == TEMP_HIGH))
  {
    return ENV_RISK_WARNING;
  }
  return ENV_RISK_NORMAL;
}

/* 湿度帯を個別危険度にする。 */
static EnvRisk hum_to_risk(HumState hum_state)
{
  if ((hum_state == HUM_DRY) || (hum_state == HUM_WET))
  {
    return ENV_RISK_CRITICAL;
  }
  if ((hum_state == HUM_LOW) || (hum_state == HUM_HIGH))
  {
    return ENV_RISK_WARNING;
  }
  return ENV_RISK_NORMAL;
}

/* 温度と湿度の個別危険度から総合危険度を決める。 */
static EnvRisk combine_risk(EnvRisk temp_risk, EnvRisk hum_risk)
{
  if ((temp_risk == ENV_RISK_CRITICAL) || (hum_risk == ENV_RISK_CRITICAL))
  {
    return ENV_RISK_CRITICAL;
  }
  if ((temp_risk == ENV_RISK_WARNING) && (hum_risk == ENV_RISK_WARNING))
  {
    return ENV_RISK_DANGER;
  }
  if ((temp_risk == ENV_RISK_WARNING) || (hum_risk == ENV_RISK_WARNING))
  {
    return ENV_RISK_WARNING;
  }
  return ENV_RISK_NORMAL;
}

/* 高い方の総合危険度を返す。 */
static EnvRisk higher_risk(EnvRisk left, EnvRisk right)
{
  if (left > right)
  {
    return left;
  }
  return right;
}

/* WARNING 検知回数を足し、下限未満なら底上げする。 */
static void add_warn_count(uint8_t floor_count)
{
  if (state.warn_count < 255U)
  {
    state.warn_count++;
  }
  if (state.warn_count < floor_count)
  {
    state.warn_count = floor_count;
  }
  state.normal_streak = 0U;
}

/* 今回の総合危険度で検知回数と正常連続を更新する。 */
static void update_warn_count(EnvRisk instant)
{
  if (instant == ENV_RISK_WARNING)
  {
    add_warn_count(0U);
    return;
  }
  if (instant == ENV_RISK_DANGER)
  {
    add_warn_count(2U);
    return;
  }
  if (instant == ENV_RISK_CRITICAL)
  {
    add_warn_count(3U);
    return;
  }

  state.normal_streak++;
  if (state.normal_streak >= NORMAL_RESET_COUNT)
  {
    state.warn_count = 0U;
    state.normal_streak = 0U;
  }
  else if (state.warn_count > 0U)
  {
    state.warn_count--;
  }
}

/* LCD 向けの表示状態を埋める。 */
static void fill_view(EnvView *view, uint32_t now_sec, uint8_t sensor_error)
{
  view->sensor_error = sensor_error;
  view->has_value = state.has_value;
  view->temp_deci_c = state.last_temp;
  view->hum_deci_rh = state.last_hum;
  view->risk = state.risk;
  view->show_minutes = 0U;
  view->minutes = 0U;

  if (sensor_error != 0U)
  {
    if (state.has_value != 0U)
    {
      view->show_minutes = 1U;
      view->minutes = elapsed_minutes(now_sec, state.last_ok_sec);
    }
    return;
  }

  if (state.risk != ENV_RISK_NORMAL)
  {
    view->show_minutes = 1U;
    view->minutes = elapsed_minutes(now_sec, state.episode_start_sec);
  }
}

/* 異常通知を載せたら、送信成功まで復帰の資格にしない。 */
static void arm_alert(uint8_t pushed)
{
  if (pushed != 0U)
  {
    pending_alert = 1U;
  }
}

/* 危険度の遷移に合わせて通知種別を決める。 */
static void notify_risk(EnvRisk prev, EnvRisk now_risk, uint32_t now_sec,
                        WifiNote *notes, uint8_t *note_count, uint8_t note_cap)
{
  if (now_risk == ENV_RISK_NORMAL)
  {
    if ((prev != ENV_RISK_NORMAL) && (state.alert_sent != 0U))
    {
      push_note(notes, note_count, note_cap, WIFI_NOTE_RECOVERY, ENV_RISK_NORMAL);
    }
    state.warning_notified = 0U;
    state.warning_repeated = 0U;
    state.alert_sent = 0U;
    state.danger_notify_ordinal = 0U;
    danger_alert_done = 0U;
    return;
  }

  if (now_risk == ENV_RISK_WARNING)
  {
    if ((state.warning_notified == 0U) && (state.warn_count >= WARNING_CONFIRM_COUNT))
    {
      if (push_note(notes, note_count, note_cap, WIFI_NOTE_ALERT, ENV_RISK_WARNING) != 0U)
      {
        state.warning_notified = 1U;
        state.warning_notify_sec = now_sec;
        pending_warn_first = 1U;
      }
    }
    else if ((state.warning_notified != 0U) && (state.warning_repeated == 0U) &&
             ((now_sec - state.warning_notify_sec) >= WARNING_REPEAT_SEC))
    {
      if (push_note(notes, note_count, note_cap, WIFI_NOTE_REPEAT, ENV_RISK_WARNING) != 0U)
      {
        state.warning_repeated = 1U;
      }
    }
    return;
  }

  if (now_risk == ENV_RISK_DANGER)
  {
    if ((prev != ENV_RISK_DANGER) || (danger_alert_done == 0U))
    {
      if (push_note(notes, note_count, note_cap, WIFI_NOTE_ALERT, ENV_RISK_DANGER) != 0U)
      {
        if (state.danger_notify_ordinal == 0U)
        {
          state.danger_notify_ordinal = 1U;
        }
        pending_alert = 1U;
        pending_danger_open = 1U;
      }
    }
    else if ((state.danger_notify_ordinal > 0U) &&
             (state.danger_notify_ordinal <= (UINT32_MAX / DANGER_NOTIFY_SEC)) &&
             ((now_sec - state.danger_since_sec) >=
              (state.danger_notify_ordinal * DANGER_NOTIFY_SEC)))
    {
      if (push_note(notes, note_count, note_cap, WIFI_NOTE_REPEAT, ENV_RISK_DANGER) != 0U)
      {
        state.danger_notify_ordinal++;
        pending_alert = 1U;
      }
    }
    return;
  }

  if (prev != ENV_RISK_CRITICAL)
  {
    arm_alert(push_note(notes, note_count, note_cap, WIFI_NOTE_ALERT, ENV_RISK_CRITICAL));
  }
  else
  {
    arm_alert(push_note(notes, note_count, note_cap, WIFI_NOTE_REPEAT, ENV_RISK_CRITICAL));
  }
}

/* 取得成功時に判定と通知状態を進める。 */
static void apply_success(const EnvReading *reading, uint32_t now_sec,
                          WifiNote *notes, uint8_t *note_count, uint8_t note_cap)
{
  EnvRisk prev;
  EnvRisk instant;
  EnvRisk now_risk;
  TempState temp_state;
  HumState hum_state;

  temp_state = classify_temp(reading->temp_deci_c);
  hum_state = classify_hum(reading->hum_deci_rh);
  state.temp_state = temp_state;
  state.hum_state = hum_state;

  instant = combine_risk(temp_to_risk(temp_state), hum_to_risk(hum_state));
  update_warn_count(instant);

  prev = state.risk;
  if (instant == ENV_RISK_NORMAL)
  {
    now_risk = ENV_RISK_NORMAL;
  }
  else if (prev == ENV_RISK_NORMAL)
  {
    now_risk = instant;
  }
  else
  {
    /* 一度上がった危険度は、両方が NORMAL に戻るまで下げない。 */
    now_risk = higher_risk(prev, instant);
  }

  if (now_risk == ENV_RISK_WARNING)
  {
    if (prev != ENV_RISK_WARNING)
    {
      state.warning_since_sec = now_sec;
    }
    if ((now_sec - state.warning_since_sec) >= WARNING_ESCALATION_SEC)
    {
      now_risk = ENV_RISK_DANGER;
    }
  }
  if (now_risk == ENV_RISK_DANGER)
  {
    if (prev != ENV_RISK_DANGER)
    {
      state.danger_since_sec = now_sec;
    }
    if ((now_sec - state.danger_since_sec) >= DANGER_ESCALATION_SEC)
    {
      now_risk = ENV_RISK_CRITICAL;
    }
  }

  if ((prev == ENV_RISK_NORMAL) && (now_risk != ENV_RISK_NORMAL))
  {
    state.episode_start_sec = now_sec;
    state.warning_notified = 0U;
    state.warning_repeated = 0U;
  }

  state.last_temp = reading->temp_deci_c;
  state.last_hum = reading->hum_deci_rh;
  state.last_ok_sec = now_sec;
  state.has_value = 1U;
  state.risk = now_risk;

  notify_risk(prev, now_risk, now_sec, notes, note_count, note_cap);

  if (state.system != SYS_WIFI_ERROR)
  {
    state.system = SYS_NORMAL;
  }
}

/* 取得失敗時は危険度を変えず、センサー異常にする。 */
static void apply_failure(WifiNote *notes, uint8_t *note_count, uint8_t note_cap)
{
  /* 失敗回は検知回数を変えず、正常の連続だけ切る。 */
  state.normal_streak = 0U;
  state.system = SYS_SENSOR_ERROR;
  push_note(notes, note_count, note_cap, WIFI_NOTE_SENSOR_ERROR, state.risk);
}

void EnvJudge_Apply(const EnvReading *reading, uint32_t now_sec,
                    EnvView *view, WifiNote *notes, uint8_t *note_count,
                    uint8_t note_cap)
{
  uint8_t sensor_error;

  if (note_count != NULL)
  {
    *note_count = 0U;
  }
  if ((reading == NULL) || (view == NULL) || (note_count == NULL))
  {
    return;
  }

  sensor_error = 0U;
  if (reading->ok == 0U)
  {
    sensor_error = 1U;
    apply_failure(notes, note_count, note_cap);
  }
  else
  {
    apply_success(reading, now_sec, notes, note_count, note_cap);
  }

  fill_view(view, now_sec, sensor_error);
  consider_heartbeat(now_sec, notes, note_count, note_cap);
}

void EnvJudge_NoteWifi(uint8_t failed)
{
  if (state.system == SYS_SENSOR_ERROR)
  {
    return;
  }
  if (failed != 0U)
  {
    state.system = SYS_WIFI_ERROR;
  }
  else if (state.system == SYS_WIFI_ERROR)
  {
    state.system = SYS_NORMAL;
  }
}

void EnvJudge_SetNotifyMode(uint8_t on)
{
  notify_mode_on = on;
}

/* 記録済みの秒へ差分を足す。0 は未開始のままにする。 */
static void shift_sec(uint32_t *sec, uint32_t delta)
{
  if (*sec != 0U)
  {
    *sec += delta;
  }
}

void EnvJudge_AcceptClock(uint32_t old_sec, uint32_t new_sec)
{
  uint32_t delta;

  if (new_sec >= old_sec)
  {
    delta = new_sec - old_sec;
    shift_sec(&state.last_ok_sec, delta);
    shift_sec(&state.episode_start_sec, delta);
    shift_sec(&state.warning_since_sec, delta);
    shift_sec(&state.warning_notify_sec, delta);
    shift_sec(&state.danger_since_sec, delta);
  }
  if (HEARTBEAT_SEC != 0U)
  {
    state.heartbeat_period = new_sec / HEARTBEAT_SEC;
  }
}

void EnvJudge_CopyReport(const WifiNote *notes, uint8_t note_count, EnvReport *report)
{
  uint8_t index;

  if (report == NULL)
  {
    return;
  }
  report->notify = 0U;
  report->periodic = 0U;
  report->risk = state.risk;
  report->temp_state = state.temp_state;
  report->hum_state = state.hum_state;
  report->temp_deci_c = state.last_temp;
  report->hum_deci_rh = state.last_hum;

  if (notes == NULL)
  {
    return;
  }
  for (index = 0U; index < note_count; index++)
  {
    if (notes[index].kind == WIFI_NOTE_HEARTBEAT)
    {
      report->periodic = 1U;
    }
    else if (notes[index].kind == WIFI_NOTE_SENSOR_ERROR)
    {
      /* 危険度が正常のまま出すと、中継が正常復帰にする。 */
      if (state.risk != ENV_RISK_NORMAL)
      {
        report->notify = 1U;
      }
    }
    else
    {
      report->notify = 1U;
    }
  }
}

uint8_t EnvJudge_HasValue(void)
{
  return state.has_value;
}

void EnvJudge_CommitSend(uint8_t sent)
{
  if ((sent != 0U) && (pending_alert != 0U))
  {
    state.alert_sent = 1U;
  }
  if ((sent != 0U) && (pending_danger_open != 0U))
  {
    danger_alert_done = 1U;
  }
  if ((sent == 0U) && (pending_warn_first != 0U))
  {
    state.warning_notified = 0U;
  }
  pending_alert = 0U;
  pending_warn_first = 0U;
  pending_danger_open = 0U;
}
