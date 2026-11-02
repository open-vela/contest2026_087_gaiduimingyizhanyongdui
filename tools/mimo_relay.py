#!/usr/bin/env python3
"""
FOCUS AIoT - MiMo HTTPS 中继 (tools/mimo_relay.py)
====================================================
设备 (ESP32-S3-EYE) 没有 TLS 栈, 无法直连 https 的 MiMo endpoint。
本中继部署在 PC / 租用服务器上:
  设备 --(http)--> 中继 --(https)--> MiMo

设备把 OpenAI 兼容的 chat/completions 请求体 (含 base64 图片) POST 到中继,
中继原样转发给 MiMo, 再把响应回传。
顺带把最近一帧存为 PREVIEW_JPEG_PATH, 浏览器 GET /preview 实时查看画面
(设备侧零改动, 只是搭 MiMo 识图的便车)。

部署:
  MIMO_API_KEY=<mimo的key> RELAY_TOKEN=<中继访问令牌> python3 mimo_relay.py
  或填在下面的默认值里。

设备侧配置 (perception.c):
  #define MIMO_ENDPOINT_DEFAULT "http://<服务器IP>:8600/v1/chat/completions"
设备需在 Authorization 头带 RELAY_TOKEN (wifi_set_http_auth 设置)。

安全:
  RELAY_TOKEN 用于防止公网其他人蹭用 MiMo 额度 (未设置则不校验, 仅限内网测试)。

网页端 (新增):
  GET /login        登录页 (演示鉴权, 无真实后端)
  GET /history      学习历史记录页 (需要本地登录态, 数据为演示数据 + 真实报告)
  GET /history.json 历史记录数据接口
  POST /api/login   演示登录接口, 只回一个本地会话标识, 不校验密码
  GET /preview      (原有) 实时画面预览
  GET /report       (原有) 单次学习报告
"""

import base64
import http.server
import json
import os
import sys
import time
import urllib.error
import urllib.request
import uuid

# MiMo 专属 endpoint (tokenplan)
MIMO_URL = os.environ.get(
    "MIMO_URL",
    "https://token-plan-cn.xiaomimimo.com/v1/chat/completions",
)

# MiMo 的 API key (放在服务器, 不必烧进设备)
MIMO_API_KEY = os.environ.get(
    "MIMO_API_KEY",
    "tp-chi5rilj9dbox2n7kcx3esep6mzi2r5b1knu6wl8wf2cped5",
)

# 中继访问令牌: 设备 Authorization: Bearer <RELAY_TOKEN>
# 留空 = 不校验 (仅建议内网联调用)
RELAY_TOKEN = os.environ.get("RELAY_TOKEN", "")

# 本机手机使用检测服务 (G:/phone-use-detection, 见《手机使用检测服务-使用说明.md》)。
# 设置后走本机模型 (推荐, 免费/快/隐私), 留空则回退 MiMo 云端。
# 注意: 必须填运行检测服务那台电脑的**局域网 IP**, 不要用 127.0.0.1;
#       且中继所在服务器要能访问到该 IP (同一局域网, 或做内网穿透/端口映射)。
DETECT_URL = os.environ.get("DETECT_URL", "")

# 本机检测服务超时 (首次请求加载模型约 2s, 之后 70~80ms/张)
DETECT_TIMEOUT = float(os.environ.get("DETECT_TIMEOUT", "15"))

# 学习报告: 设备一局结束 POST 统计到 /report, 中继调 MiMo 生成建议并存储,
# 浏览器 GET /report 查看完整报告 (设备屏仅 240x240, 显示不下长文本)。
REPORT_JSON_PATH = os.environ.get("REPORT_JSON_PATH", "/tmp/report.json")

# 历史记录: 每完成一局, 中继把本局摘要追加到这个 JSONL 文件 (一行一条),
# 网页 /history 从这里读真实记录。REPORT_JSON_PATH 只保存"最新一局",
# 本文件负责"多局累积"。
HISTORY_PATH = os.environ.get("HISTORY_PATH", "/tmp/focus_history.jsonl")
# 历史为空时是否用固定的演示数据兜底 (首次部署/演示用)
HISTORY_SEED_DEMO = os.environ.get("HISTORY_SEED_DEMO", "1") != "0"
# 历史最多保留的条数 (超出后按时间丢弃最旧的)
HISTORY_MAX_ROWS = int(os.environ.get("HISTORY_MAX_ROWS", "500"))

# 让 MiMo 生成学习建议的提示词 —— 两种模式各一套, 风格差异明显, 便于对比展示。
REPORT_PROMPT_STRICT = (
    "你是严格的学习监督教练。根据下面的学习数据, 写一段 50 到 100 字的反馈。"
    "要求: 直接点出最突出的问题(如玩手机次数多、专注度低), 语气坚定、就事论事、不绕弯子, "
    "最后给出 1 条明确的改进要求。不要标题、不要markdown、不要引号、不要表情符号, 直接输出正文。"
)

REPORT_PROMPT_GENTLE = (
    "你是温暖的学习陪伴教练。根据下面的学习数据, 写一段 50 到 100 字的反馈。"
    "要求: 先具体肯定对方做得好的地方(如坚持的时长), 再温和地提出 1 条小建议, "
    "语气亲切、给人信心, 像朋友在鼓励。不要标题、不要markdown、不要引号、不要表情符号, 直接输出正文。"
)

PORT = int(os.environ.get("PORT", "8600"))

# 生成学习建议用的文字模型 (MiMo, OpenAI 兼容); 与应用层识图用的模型无关。
# MiMo 已下线 mimo-v2.5 (请求返回 400 Unsupported model), 现役为
# mimo-v2.6-flash / mimo-v2.6-pro。本作品只要一段 50~100 字短建议,
# flash 足够且更快更省。
MIMO_MODEL = os.environ.get("MIMO_MODEL", "mimo-v2.6-flash")

# 新模型 (v2.6) 是推理模型: 默认先输出思考过程, reasoning_tokens 也计入
# max_completion_tokens。短建议用不上思考, 关掉它 —— 实测 thinking=disabled
# 时 reasoning_tokens = 0, 建议质量不受影响, 响应也更快。
# 若某天该参数不被接受, _mimo_post() 会自动去掉它重试一次。
MIMO_THINKING = os.environ.get("MIMO_THINKING", "disabled")


def _mimo_body(model, max_tokens, messages):
    """构造 MiMo 请求体 (统一处理思考开关)。"""
    body = {"model": model, "max_completion_tokens": max_tokens,
            "messages": messages}
    if MIMO_THINKING == "disabled":
        body["thinking"] = {"type": "disabled"}
    return body


def _mimo_post(body, timeout=60):
    """POST 到 MiMo 并返回解析后的 JSON。

    若服务端不接受 thinking 参数 (400), 去掉后重试一次 —— 避免因为一个
    可选参数把整条建议链路打挂。
    """
    def _send(payload):
        req = urllib.request.Request(
            MIMO_URL, data=json.dumps(payload).encode(),
            headers={"Content-Type": "application/json",
                     "Authorization": "Bearer " + MIMO_API_KEY})
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return json.loads(resp.read().decode("utf-8", "replace"))

    try:
        return _send(body)
    except urllib.error.HTTPError as e:
        if "thinking" in body and e.code == 400:
            retry = {k: v for k, v in body.items() if k != "thinking"}
            print("[relay] MiMo 拒绝 thinking 参数, 去掉后重试")
            return _send(retry)
        raise


def _at(seq, i):
    """安全取序列第 i 项 (缺失返回 0)。"""
    try:
        return seq[i]
    except Exception:  # noqa: BLE001
        return 0


def _score_grade(score):
    """专注度评分 → 等级文字 (与设备端 UI 一致)。"""
    if score >= 90:
        return "优秀"
    if score >= 75:
        return "良好"
    if score >= 60:
        return "及格"
    return "需努力"


def compute_achievements(stats, distraction_by_type):
    """根据本局统计计算成就。

    返回**全部**成就（含未解锁），由前端区分"已获得 / 待解锁"——
    只返回已获得的会让新用户看到空板块，展示效果差。

    阈值留出梯度：常规一局能拿 3~5 项，90 分 / 60 分钟 / 零分心属于挑战项。
    判据全部来自设备实际上报的四项统计（总时长、有效时长、专注度、分类分心次数），
    不引入额外数据源。
    """
    total_min = int(stats.get("total_min", 0))
    eff_min = int(stats.get("effective_min", 0))
    score = int(stats.get("focus_score", 0))

    d = list(distraction_by_type or [0, 0, 0, 0])[:4]
    d += [0] * (4 - len(d))
    playing, glancing, away, drowsy = d[0], d[1], d[2], d[3]
    total_dist = playing + glancing + away + drowsy
    ratio = (float(eff_min) / total_min) if total_min > 0 else 0.0

    return [
        {"id": "first_session", "name": "首战告捷", "desc": "完成一次专注监测",
         "tier": "bronze", "unlocked": True},
        {"id": "persistent", "name": "坚毅前行", "desc": "单次学习 ≥ 40 分钟",
         "tier": "bronze", "unlocked": total_min >= 40},
        {"id": "stay_seated", "name": "稳坐如山", "desc": "全程没有离座",
         "tier": "bronze", "unlocked": away == 0},
        {"id": "steady_focus", "name": "稳扎稳打", "desc": "专注度 ≥ 70 分",
         "tier": "silver", "unlocked": score >= 70},
        {"id": "efficient", "name": "高效学习者", "desc": "有效学习占比 ≥ 80%",
         "tier": "silver", "unlocked": ratio >= 0.80},
        {"id": "phone_free", "name": "手机绝缘体", "desc": "全程没有玩手机",
         "tier": "silver", "unlocked": playing == 0},
        {"id": "deep_focus", "name": "心流时刻", "desc": "专注度 ≥ 90 分",
         "tier": "gold", "unlocked": score >= 90},
        {"id": "marathon", "name": "马拉松学霸", "desc": "单次学习 ≥ 60 分钟",
         "tier": "gold", "unlocked": total_min >= 60},
        {"id": "flawless", "name": "无暇可分", "desc": "整局零分心",
         "tier": "gold", "unlocked": total_dist == 0},
    ]


def _local_advice(stats, mode="strict"):
    """MiMo 不可用时的兜底建议 (规则模板, 保证网页/设备总有内容)。"""
    eff = int(stats.get("effective_min", 0))
    score = int(stats.get("focus_score", 0))
    if mode == "gentle":
        head = "你今天有效学习了 %d 分钟, 已经很棒了!" % eff
    else:
        head = "你坚持了 %d 分钟, 专注度 %d 分。" % (eff, score)
    return head + "下次试着把手机放到伸手够不到的地方, 会更容易保持专注。"

# 网页预览: 把设备 POST 来的最近一帧解码存盘, 浏览器 /preview 轮询查看。
PREVIEW_JPEG_PATH = os.environ.get("PREVIEW_JPEG_PATH", "/tmp/preview.jpg")

# /preview 页面: 单文件 HTML, JS 每 2s 用时间戳绕过缓存拉取最新帧。
PREVIEW_PAGE = """<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>FOCUS AIoT · 实时预览</title>
<style>
  :root{--bg:#0b1220;--card:#131e30;--line:#22304a;--fg:#eef4ff;
        --muted:#91a0b5;--ok:#00c853;--warn:#ffb300;--err:#ff5252}
  *{box-sizing:border-box}
  body{margin:0;background:var(--bg);color:var(--fg);min-height:100vh;
       font-family:-apple-system,"PingFang SC","Microsoft YaHei",sans-serif;
       display:flex;justify-content:center;align-items:flex-start;
       padding:24px 16px}
  .wrap{width:100%;max-width:720px}
  .head{display:flex;align-items:center;justify-content:space-between;
        margin-bottom:14px;flex-wrap:wrap;gap:8px}
  .title{font-size:19px;font-weight:600;letter-spacing:.5px}
  .dot{display:inline-block;width:9px;height:9px;border-radius:50%;
       background:var(--muted);margin-right:7px;vertical-align:middle}
  .dot.live{background:var(--ok);box-shadow:0 0 0 0 rgba(0,200,83,.7);
            animation:pulse 1.8s infinite}
  .dot.stale{background:var(--warn)}
  .dot.wait{background:var(--muted)}
  @keyframes pulse{
    0%{box-shadow:0 0 0 0 rgba(0,200,83,.6)}
    70%{box-shadow:0 0 0 9px rgba(0,200,83,0)}
    100%{box-shadow:0 0 0 0 rgba(0,200,83,0)}}
  .state{font-size:13px;color:var(--muted)}
  .card{background:var(--card);border:1px solid var(--line);border-radius:14px;
        padding:12px}
  .stage{position:relative;width:100%;aspect-ratio:4/3;background:#05080f;
         border-radius:10px;overflow:hidden;display:flex;
         align-items:center;justify-content:center}
  .stage img{width:100%;height:100%;object-fit:contain;display:none;
             image-rendering:pixelated}
  .stage img.on{display:block}
  .ph{color:var(--muted);font-size:14px;text-align:center;line-height:1.8}
  .bar{display:flex;justify-content:space-between;align-items:center;
       margin-top:12px;font-size:12px;color:var(--muted);flex-wrap:wrap;gap:6px}
  .k{color:#5d6b80}
  .foot{margin-top:14px;font-size:12px;color:#5d6b80;text-align:center}
  a{color:var(--ok);text-decoration:none}
</style>
</head>
<body>
<div class="wrap">
  <div class="head">
    <div class="title"><span class="dot wait" id="dot"></span>FOCUS AIoT · 实时预览</div>
    <div class="state" id="state">连接中…</div>
  </div>

  <div class="card">
    <div class="stage">
      <img id="pv" alt="摄像头画面">
      <div class="ph" id="ph">等待设备上传画面…<br><span style="font-size:12px">
        设备进入「监测」状态后开始采集</span></div>
    </div>
    <div class="bar">
      <span><span class="k">最后更新</span> <b id="ts">--:--:--</b></span>
      <span><span class="k">画面大小</span> <b id="sz">--</b></span>
      <span><span class="k">刷新</span> 每 2 秒自动</span>
    </div>
  </div>

  <div class="foot">设备约每 5 秒采集一帧 ·
    <a href="/report">查看学习报告</a></div>
</div>

<script>
var img   = document.getElementById('pv');
var ph    = document.getElementById('ph');
var dot   = document.getElementById('dot');
var state = document.getElementById('state');
var tsEl  = document.getElementById('ts');
var szEl  = document.getElementById('sz');
var url   = null;          // 当前 objectURL, 用于释放
var lastOk = 0;            // 上次成功拿到画面的时间

function pad(n){ return (n < 10 ? '0' : '') + n; }
function clock(){ var d = new Date();
  return pad(d.getHours())+':'+pad(d.getMinutes())+':'+pad(d.getSeconds()); }

// 用 fetch + no-store 强制拿最新帧: 避免浏览器缓存导致画面不更新
function tick(){
  fetch('/preview.jpg?t=' + Date.now(), {cache:'no-store'})
    .then(function(r){
      if (!r.ok) throw new Error('no frame');
      return r.blob();
    })
    .then(function(b){
      if (url) URL.revokeObjectURL(url);
      url = URL.createObjectURL(b);
      img.src = url;
      img.classList.add('on');
      ph.style.display = 'none';
      lastOk = Date.now();
      tsEl.textContent = clock();
      szEl.textContent = (b.size/1024).toFixed(1) + ' KB';
      dot.className = 'dot live';
      state.textContent = '实时';
    })
    .catch(function(){
      // 还没收到画面, 或设备未在采集
      var age = Date.now() - lastOk;
      if (!lastOk) {
        dot.className = 'dot wait';
        state.textContent = '等待画面';
      } else if (age > 15000) {
        dot.className = 'dot stale';
        state.textContent = '画面已停止更新';
      } else {
        dot.className = 'dot stale';
        state.textContent = '暂时无新帧';
      }
    });
}
tick();
setInterval(tick, 2000);
</script>
</body>
</html>
"""

# /report 页面: 完整学习报告 (统计数据 + MiMo 建议正文)。
# 设备屏 240x240 只显示一行提示, 长文本在这里看。
REPORT_PAGE = """<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>FOCUS AIoT 学习报告</title>
<style>
  body{margin:0;background:#0b1220;color:#eef4ff;
       font-family:-apple-system,"PingFang SC","Microsoft YaHei",sans-serif;
       display:flex;justify-content:center;padding:24px 16px}
  .card{background:#131e30;border:1px solid #22304a;border-radius:14px;
        max-width:640px;width:100%;padding:24px}
  h1{font-size:20px;margin:0 0 4px}
  .sub{color:#91a0b5;font-size:13px;margin-bottom:20px}
  .score{font-size:44px;font-weight:700;line-height:1}
  .grade{color:#00c853;font-size:15px;margin-left:8px}
  .bar{height:8px;background:#22304a;border-radius:4px;margin:10px 0 22px;
       overflow:hidden}
  .bar>i{display:block;height:100%;background:#00c853}
  .grid{display:grid;grid-template-columns:repeat(2,1fr);gap:12px;
        margin-bottom:20px}
  .cell{background:#0f1a2b;border-radius:10px;padding:12px}
  .k{color:#91a0b5;font-size:12px}
  .v{font-size:19px;font-weight:600;margin-top:4px}
  h2{font-size:15px;margin:0 0 10px;color:#00c853}
  .advice{background:#0f1a2b;border-left:3px solid #00c853;border-radius:8px;
          padding:14px 16px;line-height:1.75;font-size:15px;white-space:pre-wrap}
  .foot{color:#5d6b80;font-size:12px;margin-top:18px}
  .tabs{display:flex;gap:8px;margin-bottom:12px}
  .tab{flex:1;padding:9px 0;border-radius:9px;cursor:pointer;font-size:14px;
       background:#0f1a2b;color:#91a0b5;border:1px solid #22304a;
       font-family:inherit;transition:.15s}
  .tab:hover{color:#eef4ff}
  .tab.active{background:#00c853;color:#04120a;border-color:#00c853;
              font-weight:600}
  #tab_gentle.active{background:#ffb300;border-color:#ffb300}
  /* ---- 成就 ---- */
  h2.ach-h{margin-top:24px}
  .ach-sum{color:#91a0b5;font-size:12px;margin:-4px 0 12px}
  .ach{display:grid;grid-template-columns:repeat(3,1fr);gap:10px}
  .badge{background:#0f1a2b;border:1px solid #22304a;border-radius:10px;
         padding:12px 8px;text-align:center;opacity:.38;transition:.2s}
  .badge.on{opacity:1;background:#0e2118;border-color:#2f7d4f}
  .badge .ico{font-size:24px;line-height:1.1;filter:grayscale(1)}
  .badge.on .ico{filter:none}
  .badge .nm{font-size:13px;font-weight:600;margin-top:6px}
  .badge .ds{font-size:11px;color:#91a0b5;margin-top:3px;line-height:1.4}
  .badge.t-gold.on{border-color:#ffb300;
                   box-shadow:0 0 0 1px rgba(255,179,0,.28) inset}
  .badge.t-silver.on{border-color:#9fb3c8}
  .badge.t-bronze.on{border-color:#cd7f32}
</style>
</head>
<body>
<div class="card">
  <h1>学习报告</h1>
  <div class="sub" id="hdr">加载中...</div>
  <div><span class="score" id="score">--</span><span class="grade" id="grade"></span></div>
  <div class="bar"><i id="bar" style="width:0"></i></div>
  <div class="grid">
    <div class="cell"><div class="k">总时长</div><div class="v" id="total">--</div></div>
    <div class="cell"><div class="k">有效学习</div><div class="v" id="effective">--</div></div>
    <div class="cell"><div class="k">玩手机 / 看手机</div><div class="v" id="phone">--</div></div>
    <div class="cell"><div class="k">离座 / 瞌睡</div><div class="v" id="away">--</div></div>
  </div>
  <h2>教练建议</h2>
  <div class="tabs">
    <button class="tab active" id="tab_strict" onclick="pick('strict')">严格模式</button>
    <button class="tab" id="tab_gentle" onclick="pick('gentle')">鼓励模式</button>
  </div>
  <div class="advice" id="advice">加载中...</div>
  <h2 class="ach-h">本次专注获得成就如下</h2>
  <div class="ach-sum" id="achsum"></div>
  <div class="ach" id="ach"></div>
  <div class="foot" id="foot"></div>
</div>
<script>
function fmt(sec){
  var m = Math.floor(sec/60), s = sec%60;
  if (m >= 60) { return Math.floor(m/60)+' 小时 '+(m%60)+' 分钟'; }
  return m+' 分 '+s+' 秒';
}
var ADV = {strict:'', gentle:''}, CUR = 'strict';
function pick(m){
  CUR = m;
  document.getElementById('tab_strict').className =
    'tab' + (m === 'strict' ? ' active' : '');
  document.getElementById('tab_gentle').className =
    'tab' + (m === 'gentle' ? ' active' : '');
  document.getElementById('advice').textContent = ADV[m] || '(暂无建议)';
}
var ACH_ICON = {first_session:'🎯', persistent:'⏳', stay_seated:'🪑',
                steady_focus:'📈', efficient:'⚡', phone_free:'📵',
                deep_focus:'🌊', marathon:'🏅', flawless:'💎'};
function renderAch(list){
  var box = document.getElementById('ach');
  var sumEl = document.getElementById('achsum');
  if (!list || !list.length){ box.innerHTML = ''; sumEl.textContent = ''; return; }
  var got = 0;
  for (var i = 0; i < list.length; i++) if (list[i].unlocked) got++;
  sumEl.textContent = '本局解锁 ' + got + ' / ' + list.length + ' 项';
  box.innerHTML = list.map(function(a){
    return '<div class="badge t-' + (a.tier || 'bronze') +
           (a.unlocked ? ' on' : '') + '">' +
           '<div class="ico">' + (ACH_ICON[a.id] || '🏆') + '</div>' +
           '<div class="nm">' + a.name + '</div>' +
           '<div class="ds">' + a.desc + '</div></div>';
  }).join('');
}
function load(){
  fetch('/report.json?t='+Date.now()).then(function(r){
    if(!r.ok) throw new Error('no report');
    return r.json();
  }).then(function(d){
    var s = d.stats || {};
    document.getElementById('hdr').textContent =
      (s.mode === 'gentle' ? '鼓励模式' : '严格模式') + ' · ' +
      (d.generated_at || '');
    document.getElementById('score').textContent = s.focus_score;
    document.getElementById('grade').textContent = d.grade || '';
    document.getElementById('bar').style.width = s.focus_score + '%';
    document.getElementById('total').textContent = fmt(s.total_duration_sec);
    document.getElementById('effective').textContent = fmt(s.effective_duration_sec);
    var dbt = d.distraction_by_type || [0,0,0,0];
    document.getElementById('phone').textContent = dbt[0]+' / '+dbt[1]+' 次';
    document.getElementById('away').textContent  = dbt[2]+' / '+dbt[3]+' 次';
    ADV.strict = d.advice || '';
    ADV.gentle = d.advice_gentle || d.advice || '';
    document.getElementById('advice').textContent = ADV[CUR] || '(暂无建议)';
    document.getElementById('foot').textContent =
      '分心合计 ' + (s.distraction_count||0) + ' 次';
    renderAch(d.achievements);
  }).catch(function(){
    document.getElementById('hdr').textContent = '暂无报告 (完成一次学习后自动生成)';
    document.getElementById('advice').textContent = '等待设备上传学习数据...';
    renderAch([]);
  });
}
load();
setInterval(load, 5000);
</script>
</body>
</html>
"""


# ====================================================================
# 网页端扩展 (登录 / 历史记录) —— 纯前端演示, 未接真实鉴权
# ====================================================================

# 统一注入到每个页面的顶部导航。
# 用 position:fixed 脱离文档流, 因此不改变各页面原有 flex 布局;
# body 只额外补一点上边距, 原有 .wrap / .card 结构与脚本一行未动。
NAV_HTML = """
<style>
  .fnav{position:fixed;top:0;left:0;right:0;height:56px;z-index:80;
        display:flex;align-items:center;gap:6px;padding:0 18px;
        background:rgba(11,18,32,.92);backdrop-filter:blur(12px);
        border-bottom:1px solid #22304a;
        font-family:-apple-system,"PingFang SC","Microsoft YaHei",sans-serif}
  .fnav .brand{display:flex;align-items:center;gap:9px;color:#eef4ff;
        font-size:14.5px;font-weight:600;letter-spacing:.4px;
        text-decoration:none;margin-right:14px}
  .fnav .brand i{width:9px;height:9px;border-radius:3px;background:#00c853;
        box-shadow:0 0 10px rgba(0,200,83,.85);display:block}
  .fnav .nav{display:flex;gap:4px;margin-right:auto}
  .fnav .nav a{color:#91a0b5;text-decoration:none;font-size:13.5px;
        padding:7px 13px;border-radius:8px;transition:.15s;white-space:nowrap}
  .fnav .nav a:hover{color:#eef4ff;background:#131e30}
  .fnav .nav a.on{color:#04120a;background:#00c853;font-weight:600}
  .fnav .userbox{position:relative;display:flex;align-items:center}
  .fnav .loginbtn{color:#eef4ff;text-decoration:none;font-size:13.5px;
        border:1px solid #22304a;background:#131e30;padding:7px 15px;
        border-radius:8px;transition:.15s;white-space:nowrap}
  .fnav .loginbtn:hover{border-color:#00c853;color:#00c853}
  .fnav .me{display:none;align-items:center;gap:8px;background:#131e30;
        border:1px solid #22304a;border-radius:999px;padding:4px 12px 4px 4px;
        color:#eef4ff;font-size:13.5px;cursor:pointer;font-family:inherit;
        transition:.15s}
  .fnav .me:hover{border-color:#3a4a66}
  .fnav .me.on{display:inline-flex}
  .fnav .av{width:26px;height:26px;border-radius:50%;background:#00c853;
        color:#04120a;font-weight:700;font-size:12.5px;display:flex;
        align-items:center;justify-content:center}
  .fnav .menu{position:absolute;top:44px;right:0;min-width:196px;display:none;
        background:#131e30;border:1px solid #22304a;border-radius:12px;
        padding:6px;box-shadow:0 16px 40px rgba(0,0,0,.55)}
  .fnav .menu.on{display:block}
  .fnav .menu .hi{padding:9px 11px 10px;color:#5d6b80;font-size:11.5px;
        border-bottom:1px solid #22304a;margin-bottom:4px;word-break:break-all;
        line-height:1.5}
  .fnav .menu a,.fnav .menu button{display:block;width:100%;text-align:left;
        background:none;border:0;color:#91a0b5;font-size:13px;padding:9px 11px;
        border-radius:8px;cursor:pointer;text-decoration:none;
        font-family:inherit;transition:.15s}
  .fnav .menu a:hover,.fnav .menu button:hover{background:#0f1a2b;color:#eef4ff}
  .fnav .menu .out:hover{color:#ff5252}
  body{padding-top:76px !important}
  @media (max-width:600px){
    .fnav .brand span{display:none}
    .fnav .nav a{padding:7px 9px;font-size:13px}
  }
</style>
<header class="fnav">
  <a class="brand" href="/preview"><i></i><span>FOCUS AIoT</span></a>
  <nav class="nav">
    <a href="/preview">实时预览</a>
    <a href="/report">学习报告</a>
    <a href="/history">历史记录</a>
  </nav>
  <div class="userbox">
    <a class="loginbtn" id="fnavLogin" href="/login">登录</a>
    <button class="me" id="fnavMe" type="button">
      <span class="av" id="fnavAv">U</span><span id="fnavName">用户</span>
    </button>
    <div class="menu" id="fnavMenu">
      <div class="hi" id="fnavWho"></div>
      <a href="/history">我的学习记录</a>
      <a href="/report">本次学习报告</a>
      <button class="out" id="fnavLogout" type="button">退出登录</button>
    </div>
  </div>
</header>
<script>
(function(){
  var K = 'focus_user';
  function readUser(){
    try { return JSON.parse(localStorage.getItem(K) || 'null'); }
    catch (e) { return null; }
  }
  var u = readUser();
  var login = document.getElementById('fnavLogin');
  var me    = document.getElementById('fnavMe');
  var menu  = document.getElementById('fnavMenu');

  if (u) {
    login.style.display = 'none';
    me.className = 'me on';
    document.getElementById('fnavName').textContent = u.name || '用户';
    document.getElementById('fnavAv').textContent =
      (u.name || 'U').slice(0, 1).toUpperCase();
    document.getElementById('fnavWho').textContent =
      '已登录 · ' + (u.account || '');
  } else {
    login.href = '/login?next=' +
      encodeURIComponent(location.pathname + location.search);
  }

  me.addEventListener('click', function(e){
    e.stopPropagation();
    menu.className = menu.className === 'menu on' ? 'menu' : 'menu on';
  });
  document.addEventListener('click', function(){ menu.className = 'menu'; });

  document.getElementById('fnavLogout').addEventListener('click', function(){
    localStorage.removeItem(K);
    try { document.cookie = 'focus_user=;path=/;max-age=0'; } catch (e) {}
    location.href = '/login';
  });

  // 当前页高亮
  var here = location.pathname;
  var links = document.querySelectorAll('.fnav .nav a');
  for (var i = 0; i < links.length; i++) {
    if (links[i].getAttribute('href') === here) links[i].className = 'on';
  }
})();
</script>
"""


# 登录页: 左侧品牌介绍 + 右侧表单。演示环境, 任意账号密码均可登录。
LOGIN_PAGE = """<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>登录 · FOCUS AIoT</title>
<style>
  :root{--bg:#0b1220;--card:#131e30;--line:#22304a;--fg:#eef4ff;
        --muted:#91a0b5;--ok:#00c853;--warn:#ffb300;--err:#ff5252}
  *{box-sizing:border-box}
  body{margin:0;min-height:100vh;color:var(--fg);
       font-family:-apple-system,"PingFang SC","Microsoft YaHei",sans-serif;
       background:radial-gradient(820px 460px at 10% -12%,rgba(0,200,83,.16),transparent 62%),
                  radial-gradient(700px 420px at 98% 4%,rgba(0,120,255,.12),transparent 60%),
                  var(--bg);
       display:flex;align-items:center;justify-content:center;padding:76px 18px 40px}
  .shell{width:100%;max-width:920px;display:grid;grid-template-columns:1fr .92fr;
         background:var(--card);border:1px solid var(--line);border-radius:20px;
         overflow:hidden;box-shadow:0 26px 70px rgba(0,0,0,.5)}
  .brand{padding:40px 34px;border-right:1px solid var(--line);
         background:linear-gradient(155deg,#101c2e,#0b1220 65%);
         position:relative;overflow:hidden}
  .brand:after{content:"";position:absolute;width:280px;height:280px;
         right:-100px;bottom:-120px;border-radius:50%;
         background:radial-gradient(circle,rgba(0,200,83,.22),transparent 68%)}
  .logo{display:flex;align-items:center;gap:10px;font-size:15px;
         font-weight:600;letter-spacing:.6px}
  .logo i{width:10px;height:10px;border-radius:3px;background:var(--ok);
         box-shadow:0 0 12px rgba(0,200,83,.85);display:block}
  .brand h1{font-size:27px;line-height:1.4;margin:26px 0 12px;letter-spacing:.5px}
  .brand p{margin:0;color:var(--muted);font-size:13.5px;line-height:1.9}
  .bullets{margin:26px 0 0;padding:0;list-style:none;display:flex;
           flex-direction:column;gap:13px;position:relative;z-index:1}
  .bullets li{display:flex;gap:11px;align-items:flex-start;font-size:13.5px;
           color:#c3d0e2;line-height:1.6}
  .bullets b{display:block;color:var(--fg);font-weight:600;margin-bottom:1px}
  .ic{flex:0 0 26px;height:26px;border-radius:8px;background:#0f1a2b;
      border:1px solid var(--line);display:flex;align-items:center;
      justify-content:center;font-size:12px;color:var(--ok)}
  .form{padding:38px 34px}
  .form h2{margin:0 0 6px;font-size:19px}
  .form .tip{margin:0 0 24px;color:var(--muted);font-size:13px}
  label{display:block;font-size:12.5px;color:var(--muted);margin:0 0 7px}
  .field{margin-bottom:16px}
  .inp{position:relative}
  .inp input{width:100%;height:44px;background:#0f1a2b;
      border:1px solid var(--line);border-radius:10px;color:var(--fg);
      font-size:14px;padding:0 14px;font-family:inherit;outline:none;
      transition:.15s}
  .inp input::placeholder{color:#5d6b80}
  .inp input:focus{border-color:var(--ok);box-shadow:0 0 0 3px rgba(0,200,83,.13)}
  .eye{position:absolute;right:6px;top:0;height:44px;width:48px;background:none;
      border:0;color:#5d6b80;cursor:pointer;font-size:12.5px;font-family:inherit}
  .eye:hover{color:var(--fg)}
  .row{display:flex;align-items:center;justify-content:space-between;
      margin:2px 0 20px;font-size:12.5px;color:var(--muted)}
  .row label{display:flex;align-items:center;gap:7px;margin:0;cursor:pointer;
      font-size:12.5px}
  .row input{accent-color:#00c853}
  .row a{color:var(--ok);text-decoration:none}
  .btn{width:100%;height:46px;border:0;border-radius:10px;font-size:15px;
      font-weight:600;font-family:inherit;cursor:pointer;transition:.15s;
      display:flex;align-items:center;justify-content:center;gap:9px}
  .btn-main{background:var(--ok);color:#04120a}
  .btn-main:hover{background:#00e05c}
  .btn-main:disabled{opacity:.7;cursor:default}
  .btn-ghost{background:transparent;color:var(--muted);
      border:1px solid var(--line);margin-top:10px;height:42px;
      font-size:13.5px;font-weight:500}
  .btn-ghost:hover{color:var(--fg);border-color:#3a4a66}
  .msg{min-height:20px;margin:14px 0 0;font-size:12.5px;line-height:1.6}
  .msg.err{color:var(--err)}
  .msg.ok{color:var(--ok)}
  .spin{width:15px;height:15px;border-radius:50%;
      border:2px solid rgba(4,18,10,.28);border-top-color:#04120a;
      animation:sp .7s linear infinite;display:none}
  @keyframes sp{to{transform:rotate(360deg)}}
  .demo{margin-top:20px;background:#0f1a2b;border:1px dashed #2c3d5a;
      border-radius:10px;padding:11px 13px;font-size:12px;color:#8b9bb1;
      line-height:1.75}
  .demo b{color:#c3d0e2;font-weight:600}
  @media (max-width:820px){
    .shell{grid-template-columns:1fr}
    .brand{display:none}
    .form{padding:30px 22px}
  }
</style>
</head>
<body>
<div class="shell">
  <div class="brand">
    <div class="logo"><i></i>FOCUS AIoT</div>
    <h1>让每一次专注<br>都被记录</h1>
    <p>端侧视觉识别 + 云端学习教练，<br>把「今天学了多久」变成看得见的数据。</p>
    <ul class="bullets">
      <li><span class="ic">◉</span><div><b>实时画面预览</b>设备每 5 秒上传一帧，浏览器直接看</div></li>
      <li><span class="ic">◈</span><div><b>分心自动归类</b>玩手机 / 看手机 / 离座 / 瞌睡</div></li>
      <li><span class="ic">✦</span><div><b>双模式教练建议</b>严格模式与鼓励模式随时切换</div></li>
    </ul>
  </div>

  <div class="form">
    <h2>登录你的账户</h2>
    <p class="tip">登录后可查看历史学习记录与专注趋势</p>
    <form id="f" autocomplete="off">
      <div class="field">
        <label for="acc">手机号 / 邮箱</label>
        <div class="inp">
          <input id="acc" type="text" placeholder="请输入手机号或邮箱"
                 autocomplete="username">
        </div>
      </div>
      <div class="field">
        <label for="pwd">密码</label>
        <div class="inp">
          <input id="pwd" type="password" placeholder="请输入密码"
                 autocomplete="current-password">
          <button class="eye" type="button" id="eye">显示</button>
        </div>
      </div>
      <div class="row">
        <label><input type="checkbox" id="rm" checked>记住我</label>
        <a href="#" onclick="return false">忘记密码?</a>
      </div>
      <button class="btn btn-main" type="submit" id="submit">
        <span class="spin" id="spin"></span><span id="btnText">登 录</span>
      </button>
      <button class="btn btn-ghost" type="button" id="guest">访客体验（免登录查看）</button>
      <div class="msg" id="msg"></div>
    </form>
    <div class="demo"><b>演示环境</b>：任意账号 + 任意密码均可登录，
      登录状态仅保存在浏览器本地，未接入真实鉴权服务。</div>
  </div>
</div>
<script>
(function(){
  var K = 'focus_user';
  var next = new URLSearchParams(location.search).get('next') || '/history';
  var msg = document.getElementById('msg');
  var pwd = document.getElementById('pwd');

  function show(t, cls){ msg.textContent = t || ''; msg.className = 'msg ' + (cls || ''); }

  document.getElementById('eye').addEventListener('click', function(){
    var on = pwd.type === 'password';
    pwd.type = on ? 'text' : 'password';
    this.textContent = on ? '隐藏' : '显示';
  });

  try {
    var u = JSON.parse(localStorage.getItem(K) || 'null');
    if (u) show('当前已登录：' + (u.name || u.account) + '，可直接进入历史记录', 'ok');
  } catch (e) {}

  function done(user){
    localStorage.setItem(K, JSON.stringify(user));
    try {
      document.cookie = 'focus_user=' + encodeURIComponent(user.name || '') +
                        ';path=/;max-age=604800';
    } catch (e) {}
    show('登录成功，正在跳转…', 'ok');
    setTimeout(function(){ location.href = next; }, 550);
  }

  document.getElementById('f').addEventListener('submit', function(ev){
    ev.preventDefault();
    var account = document.getElementById('acc').value.trim();
    if (!account) { show('请输入手机号或邮箱', 'err'); return; }
    if (!pwd.value) { show('请输入密码', 'err'); return; }

    var btn  = document.getElementById('submit');
    var spin = document.getElementById('spin');
    var txt  = document.getElementById('btnText');
    btn.disabled = true;
    spin.style.display = 'block';
    txt.textContent = '登录中…';
    show('');

    fetch('/api/login', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({account: account, password: pwd.value})
    }).then(function(r){ return r.json(); }).then(function(d){
      if (d && d.ok) { done(d.user); }
      else { throw new Error((d && d.error) || '登录失败'); }
    }).catch(function(e){
      btn.disabled = false;
      spin.style.display = 'none';
      txt.textContent = '登 录';
      show(e.message || '网络异常，请重试', 'err');
    });
  });

  document.getElementById('guest').addEventListener('click', function(){
    done({name: '访客', account: 'guest@demo', token: 'guest',
          ts: Math.floor(Date.now() / 1000)});
  });
})();
</script>
</body>
</html>
"""


# 历史记录页: 概览指标 + 专注度趋势 + 可筛选列表 + 展开详情 (含双模式建议)。
HISTORY_PAGE = """<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>学习历史 · FOCUS AIoT</title>
<style>
  :root{--bg:#0b1220;--card:#131e30;--line:#22304a;--fg:#eef4ff;
        --muted:#91a0b5;--ok:#00c853;--warn:#ffb300;--err:#ff5252;
        --dim:#5d6b80;--cell:#0f1a2b}
  *{box-sizing:border-box}
  body{margin:0;background:var(--bg);color:var(--fg);min-height:100vh;
       font-family:-apple-system,"PingFang SC","Microsoft YaHei",sans-serif;
       display:flex;justify-content:center;padding:76px 16px 44px}
  .page{width:100%;max-width:880px}
  /* 页面头 */
  .ghead{display:flex;justify-content:space-between;align-items:flex-end;
         gap:14px;flex-wrap:wrap;margin-bottom:18px}
  .ghead h1{margin:0 0 6px;font-size:22px;letter-spacing:.4px}
  .ghead .sub{margin:0;color:var(--muted);font-size:13px}
  .who{display:flex;align-items:center;gap:10px;background:var(--card);
       border:1px solid var(--line);border-radius:12px;padding:9px 14px}
  .who .av{width:32px;height:32px;border-radius:50%;background:var(--ok);
       color:#04120a;font-weight:700;font-size:14px;display:flex;
       align-items:center;justify-content:center}
  .who .nm{font-size:13.5px;font-weight:600}
  .who .ac{font-size:11.5px;color:var(--dim);margin-top:2px}
  /* 概览 */
  .summary{display:grid;grid-template-columns:repeat(4,1fr);gap:12px;
           margin-bottom:14px}
  .metric{background:var(--card);border:1px solid var(--line);
          border-radius:12px;padding:14px}
  .metric .k{color:var(--muted);font-size:12px}
  .metric .v{font-size:22px;font-weight:700;margin-top:6px;line-height:1.1}
  .metric .v small{font-size:12px;color:var(--muted);font-weight:500;
          margin-left:2px}
  .trend{background:var(--card);border:1px solid var(--line);border-radius:12px;
         padding:14px 16px 10px;margin-bottom:18px}
  .trend .th{display:flex;justify-content:space-between;align-items:center;
         font-size:12.5px;color:var(--muted);margin-bottom:10px}
  .bars{display:flex;align-items:flex-end;gap:8px;height:76px}
  .bcol{flex:1;display:flex;flex-direction:column;justify-content:flex-end;
        align-items:center;gap:6px;height:100%}
  .bcol .bar{width:100%;max-width:34px;border-radius:5px 5px 2px 2px;
        background:#2c3d5a;transition:.3s}
  .bcol .lb{font-size:10.5px;color:var(--dim);white-space:nowrap}
  /* 筛选 */
  .filters{display:flex;align-items:center;gap:10px;flex-wrap:wrap;
           margin-bottom:14px}
  .seg{display:flex;background:var(--card);border:1px solid var(--line);
       border-radius:10px;padding:3px;gap:2px}
  .seg button{border:0;background:none;color:var(--muted);font-size:12.5px;
       padding:6px 12px;border-radius:8px;cursor:pointer;font-family:inherit;
       transition:.15s;white-space:nowrap}
  .seg button:hover{color:var(--fg)}
  .seg button.on{background:#22304a;color:var(--fg);font-weight:600}
  .seg button.on.g{background:var(--ok);color:#04120a}
  .hint{margin-left:auto;font-size:12px;color:var(--dim)}
  /* 列表 */
  .list{list-style:none;margin:0;padding:0;display:flex;flex-direction:column;
        gap:10px}
  .item{background:var(--card);border:1px solid var(--line);border-radius:13px;
        overflow:hidden;transition:.18s}
  .item:hover{border-color:#2f4a6e}
  .item.open{border-color:#2f4a6e}
  .row{display:flex;align-items:center;gap:14px;width:100%;padding:14px 16px;
       background:none;border:0;color:inherit;font-family:inherit;
       cursor:pointer;text-align:left}
  .date{flex:0 0 92px}
  .date b{display:block;font-size:15px;font-weight:600}
  .date span{font-size:11.5px;color:var(--dim)}
  .ring{flex:0 0 46px;width:46px;height:46px;border-radius:50%;
        display:flex;align-items:center;justify-content:center;
        position:relative;font-size:13px;font-weight:700;
        background:conic-gradient(var(--rc) calc(var(--p) * 1%), #1c2a40 0);
        color:var(--fg)}
  .ring:before{content:"";position:absolute;inset:4px;border-radius:50%;
        background:var(--card)}
  .ring span{position:relative;z-index:1}
  .meta{flex:1;min-width:0}
  .meta .t{font-size:13.5px;margin-bottom:6px}
  .chips{display:flex;gap:6px;flex-wrap:wrap}
  .chip{font-size:11px;color:#a9b8cc;background:#0f1a2b;border:1px solid #24334c;
        border-radius:999px;padding:2px 9px}
  .chip.hot{color:#ffb300;border-color:#4a3b18}
  .chip.grn{color:var(--ok);border-color:#1b4630}
  .badge{font-size:11px;padding:2px 8px;border-radius:999px;margin-left:6px}
  .badge.live{background:rgba(0,200,83,.14);color:var(--ok)}
  .chev{flex:0 0 auto;color:var(--dim);font-size:15px;transition:.2s}
  .item.open .chev{transform:rotate(90deg);color:var(--fg)}
  /* 展开详情 */
  .detail{display:none;padding:0 16px 16px;border-top:1px solid #1c2a40}
  .item.open .detail{display:block}
  .dgrid{display:grid;grid-template-columns:repeat(4,1fr);gap:10px;
         margin:14px 0}
  .dcell{background:var(--cell);border-radius:9px;padding:10px 12px}
  .dcell .k{font-size:11px;color:var(--muted)}
  .dcell .v{font-size:15px;font-weight:600;margin-top:3px}
  .dtitle{display:flex;align-items:center;justify-content:space-between;
          margin:4px 0 8px}
  .dtitle .h{font-size:12.5px;color:var(--ok);font-weight:600}
  .dtitle a{font-size:12px;color:var(--muted);text-decoration:none}
  .dtitle a:hover{color:var(--fg)}
  .mini{display:flex;gap:6px}
  .mini button{border:1px solid #22304a;background:#0f1a2b;color:var(--muted);
          font-size:11.5px;padding:4px 11px;border-radius:7px;cursor:pointer;
          font-family:inherit;transition:.15s}
  .mini button.on{background:var(--ok);border-color:var(--ok);
          color:#04120a;font-weight:600}
  .mini button.on.gentle{background:var(--warn);border-color:var(--warn)}
  .adv{background:var(--cell);border-left:3px solid var(--ok);border-radius:8px;
       padding:12px 14px;font-size:13.5px;line-height:1.8;color:#dbe6f5}
  .adv.gentle{border-left-color:var(--warn)}
  /* 空态 / 登录墙 */
  .empty{text-align:center;color:var(--muted);font-size:13.5px;
         padding:46px 10px;background:var(--card);border:1px dashed #2c3d5a;
         border-radius:13px;line-height:2}
  .gate{display:none;background:var(--card);border:1px solid var(--line);
        border-radius:16px;padding:38px 26px;text-align:center}
  .gate.on{display:block}
  .gate .ic2{width:52px;height:52px;border-radius:16px;margin:0 auto 16px;
        background:#0f1a2b;border:1px solid var(--line);display:flex;
        align-items:center;justify-content:center;font-size:20px;color:var(--ok)}
  .gate h2{margin:0 0 8px;font-size:17px}
  .gate p{margin:0 0 20px;color:var(--muted);font-size:13px;line-height:1.8}
  .gate .acts{display:flex;gap:10px;justify-content:center;flex-wrap:wrap}
  .gate .btn2{padding:10px 22px;border-radius:10px;font-size:13.5px;
        text-decoration:none;font-family:inherit;cursor:pointer;border:0;
        font-weight:600;transition:.15s}
  .gate .p1{background:var(--ok);color:#04120a}
  .gate .p1:hover{background:#00e05c}
  .gate .p2{background:transparent;color:var(--muted);border:1px solid var(--line);
        font-weight:500}
  .gate .p2:hover{color:var(--fg);border-color:#3a4a66}
  .foot{text-align:center;color:var(--dim);font-size:11.5px;margin-top:18px}
  @media (max-width:720px){
    .summary{grid-template-columns:repeat(2,1fr)}
    .dgrid{grid-template-columns:repeat(2,1fr)}
    .row{gap:10px;padding:12px}
    .date{flex:0 0 74px}
    .meta .t{font-size:12.5px}
    .hint{display:none}
  }
</style>
</head>
<body>
<div class="page">
  <div class="ghead">
    <div>
      <h1>学习历史</h1>
      <p class="sub" id="sub">加载中…</p>
    </div>
    <div class="who" id="who" style="display:none">
      <div class="av" id="whoAv">U</div>
      <div><div class="nm" id="whoNm">用户</div><div class="ac" id="whoAc"></div></div>
    </div>
  </div>

  <!-- 未登录: 登录墙 -->
  <div class="gate" id="gate">
    <div class="ic2">◈</div>
    <h2>登录后查看完整学习历史</h2>
    <p>历史记录需要账户身份。<br>登录后可查看每次学习的专注度、分心明细与教练建议。</p>
    <div class="acts">
      <a class="btn2 p1" id="gateLogin" href="/login?next=%2Fhistory">立即登录</a>
      <button class="btn2 p2" type="button" id="gateDemo">先看演示数据</button>
    </div>
  </div>

  <div id="content" style="display:none">
    <div class="summary">
      <div class="metric"><div class="k">学习节数</div>
        <div class="v" id="mCnt">--<small>次</small></div></div>
      <div class="metric"><div class="k">累计有效学习</div>
        <div class="v" id="mEff">--</div></div>
      <div class="metric"><div class="k">平均专注度</div>
        <div class="v" id="mAvg">--<small>分</small></div></div>
      <div class="metric"><div class="k">最佳专注度</div>
        <div class="v" id="mMax">--<small>分</small></div></div>
    </div>

    <div class="trend">
      <div class="th"><span>专注度趋势（最近 8 次）</span><span id="trendHint"></span></div>
      <div class="bars" id="bars"></div>
    </div>

    <div class="filters">
      <div class="seg" id="segRange">
        <button class="on" data-d="0" type="button">全部</button>
        <button data-d="7" type="button">近 7 天</button>
        <button data-d="30" type="button">近 30 天</button>
      </div>
      <div class="seg" id="segMode">
        <button class="on g" data-m="all" type="button">全部模式</button>
        <button data-m="strict" type="button">严格</button>
        <button data-m="gentle" type="button">鼓励</button>
      </div>
      <span class="hint" id="hint"></span>
    </div>

    <ul class="list" id="list"></ul>
    <div class="empty" id="empty" style="display:none">
      没有符合条件的学习记录<br>
      <span style="font-size:12px;color:#5d6b80">设备完成一次学习后会自动出现在这里</span>
    </div>
    <div class="foot">历史记录为演示数据，设备真实上报的最近一次学习会置顶显示</div>
  </div>
</div>

<script>
(function(){
  var K = 'focus_user';
  var USER = null;
  try { USER = JSON.parse(localStorage.getItem(K) || 'null'); } catch (e) {}

  var ALL = [], RANGE = 0, MODE = 'all', OPEN = {};
  var GRADE_C = {exc:'#00c853', good:'#66d17a', pass:'#ffb300', low:'#ff5252'};

  var gate = document.getElementById('gate');
  var content = document.getElementById('content');

  if (!USER) {
    gate.className = 'gate on';
    document.getElementById('sub').textContent = '未登录 · 请先登录账户';
  } else {
    content.style.display = 'block';
    document.getElementById('who').style.display = 'flex';
    document.getElementById('whoNm').textContent = USER.name || '用户';
    document.getElementById('whoAc').textContent = USER.account || '';
    document.getElementById('whoAv').textContent =
      (USER.name || 'U').slice(0, 1).toUpperCase();
  }

  // 先看演示数据: 生成本地访客态, 但仍留在本页
  document.getElementById('gateDemo').addEventListener('click', function(){
    USER = {name: '访客', account: 'guest@demo', token: 'guest',
            ts: Math.floor(Date.now() / 1000)};
    localStorage.setItem(K, JSON.stringify(USER));
    gate.className = 'gate';
    content.style.display = 'block';
    document.getElementById('who').style.display = 'flex';
    document.getElementById('whoNm').textContent = '访客';
    document.getElementById('whoAc').textContent = 'guest@demo';
    document.getElementById('whoAv').textContent = '访';
    document.getElementById('sub').textContent = '演示数据视图';
    var lg = document.getElementById('fnavLogin');
    if (lg) lg.style.display = 'none';
    var me = document.getElementById('fnavMe');
    if (me) {
      me.className = 'me on';
      document.getElementById('fnavName').textContent = '访客';
      document.getElementById('fnavAv').textContent = '访';
      document.getElementById('fnavWho').textContent = '已登录 · guest@demo';
    }
    load();   // 之前未登录时没有拉数据, 这里补上
  });

  function d2(n){ return (n < 10 ? '0' : '') + n; }
  function fmtDur(sec){
    var m = Math.floor(sec / 60), s = sec % 60;
    if (m >= 60) return Math.floor(m / 60) + ' 小时 ' + (m % 60) + ' 分';
    return m + ' 分 ' + s + ' 秒';
  }
  function fmtMin(sec){
    var m = Math.round(sec / 60);
    if (m >= 60) return Math.floor(m / 60) + ' 小时 ' + (m % 60) + ' 分';
    return m + ' 分钟';
  }
  function gradeOf(s){
    if (s >= 90) return {t:'优秀', c:GRADE_C.exc};
    if (s >= 75) return {t:'良好', c:GRADE_C.good};
    if (s >= 60) return {t:'及格', c:GRADE_C.pass};
    return {t:'需努力', c:GRADE_C.low};
  }
  function parts(ts){
    var d = new Date(ts * 1000);
    var wd = ['周日','周一','周二','周三','周四','周五','周六'][d.getDay()];
    return {
      day: (d.getMonth() + 1) + '/' + d.getDate(),
      time: wd + ' ' + d2(d.getHours()) + ':' + d2(d.getMinutes())
    };
  }

  function render(){
    var now = Math.floor(Date.now() / 1000);
    var rows = ALL.filter(function(it){
      if (RANGE && it.ts < now - RANGE * 86400) return false;
      if (MODE !== 'all' && (it.mode || 'strict') !== MODE) return false;
      return true;
    });

    // 概览
    var cnt = rows.length;
    var eff = 0, sum = 0, max = 0;
    rows.forEach(function(it){
      eff += it.effective_sec || 0;
      sum += it.focus_score || 0;
      if ((it.focus_score || 0) > max) max = it.focus_score;
    });
    document.getElementById('mCnt').innerHTML = cnt + '<small>次</small>';
    document.getElementById('mEff').textContent = cnt ? fmtMin(eff) : '--';
    document.getElementById('mAvg').innerHTML =
      cnt ? Math.round(sum / cnt) + '<small>分</small>' : '--';
    document.getElementById('mMax').innerHTML =
      cnt ? max + '<small>分</small>' : '--';
    document.getElementById('hint').textContent =
      '共 ' + cnt + ' 条记录';

    // 趋势条 (按时间正序取最后 8 次)
    var trend = rows.slice(0, 8).reverse();
    var html = '';
    trend.forEach(function(it){
      var g = gradeOf(it.focus_score);
      var h = Math.max(6, Math.round(it.focus_score * 0.72));
      html += '<div class="bcol"><div class="bar" style="height:' + h +
              'px;background:' + g.c + '" title="' + it.focus_score + ' 分"></div>' +
              '<div class="lb">' + parts(it.ts).day + '</div></div>';
    });
    document.getElementById('bars').innerHTML = html ||
      '<div style="color:#5d6b80;font-size:12.5px">暂无数据</div>';

    // 列表
    var list = document.getElementById('list');
    var out = '';
    rows.forEach(function(it, i){
      var g = gradeOf(it.focus_score);
      var p = parts(it.ts);
      var dist = it.distractions || [0, 0, 0, 0];
      var phone = (dist[0] || 0) + (dist[1] || 0);
      var away = (dist[2] || 0) + (dist[3] || 0);
      var modeTxt = (it.mode === 'gentle') ? '鼓励模式' : '严格模式';
      var open = OPEN[it.id] ? ' open' : '';
      out += '<li class="item' + open + '" data-id="' + it.id + '">' +
        '<button class="row" type="button" data-act="toggle">' +
          '<div class="date"><b>' + p.day + '</b><span>' + p.time + '</span></div>' +
          '<div class="ring" style="--p:' + (it.focus_score || 0) +
            ';--rc:' + g.c + '"><span>' + (it.focus_score || 0) + '</span></div>' +
          '<div class="meta">' +
            '<div class="t">' + g.t + ' · ' + modeTxt +
              (it.live ? '<span class="badge live">设备实时</span>' : '') + '</div>' +
            '<div class="chips">' +
              '<span class="chip grn">有效 ' + fmtMin(it.effective_sec || 0) + '</span>' +
              '<span class="chip' + (phone ? ' hot' : '') + '">手机 ' + phone + ' 次</span>' +
              '<span class="chip">离座/瞌睡 ' + away + ' 次</span>' +
            '</div>' +
          '</div>' +
          '<div class="chev">›</div>' +
        '</button>' +
        '<div class="detail">' +
          '<div class="dgrid">' +
            '<div class="dcell"><div class="k">总时长</div><div class="v">' +
              fmtDur(it.total_sec || 0) + '</div></div>' +
            '<div class="dcell"><div class="k">有效学习</div><div class="v">' +
              fmtDur(it.effective_sec || 0) + '</div></div>' +
            '<div class="dcell"><div class="k">玩手机 / 看手机</div><div class="v">' +
              (dist[0] || 0) + ' / ' + (dist[1] || 0) + ' 次</div></div>' +
            '<div class="dcell"><div class="k">离座 / 瞌睡</div><div class="v">' +
              (dist[2] || 0) + ' / ' + (dist[3] || 0) + ' 次</div></div>' +
          '</div>' +
          '<div class="dtitle"><span class="h">教练建议</span>' +
            '<span class="mini" data-id="' + it.id + '">' +
              '<button type="button" class="on" data-m="strict">严格</button>' +
              '<button type="button" data-m="gentle">鼓励</button>' +
            '</span>' +
          '</div>' +
          '<div class="adv" data-adv="' + it.id + '">' +
            (it.advice || '(暂无建议)') + '</div>' +
        '</div>' +
      '</li>';
    });
    list.innerHTML = out;
    document.getElementById('empty').style.display = cnt ? 'none' : 'block';
  }

  // 事件委托: 展开 / 收起
  document.getElementById('list').addEventListener('click', function(ev){
    var rowBtn = ev.target.closest ? ev.target.closest('.row') : null;
    if (rowBtn) {
      var li = rowBtn.parentNode;
      var id = li.getAttribute('data-id');
      OPEN[id] = !OPEN[id];
      li.className = 'item' + (OPEN[id] ? ' open' : '');
      return;
    }
    var mb = ev.target.closest ? ev.target.closest('.mini button') : null;
    if (mb) {
      ev.stopPropagation();
      var box = mb.parentNode;
      var iid = box.getAttribute('data-id');
      var mode = mb.getAttribute('data-m');
      var btns = box.querySelectorAll('button');
      for (var i = 0; i < btns.length; i++) btns[i].className = '';
      mb.className = 'on' + (mode === 'gentle' ? ' gentle' : '');
      var it = null;
      for (var j = 0; j < ALL.length; j++) if (ALL[j].id === iid) it = ALL[j];
      if (!it) return;
      var adv = document.querySelector('[data-adv="' + iid + '"]');
      adv.textContent = (mode === 'gentle'
        ? (it.advice_gentle || it.advice) : it.advice) || '(暂无建议)';
      adv.className = 'adv' + (mode === 'gentle' ? ' gentle' : '');
    }
  });

  // 筛选
  document.getElementById('segRange').addEventListener('click', function(ev){
    var b = ev.target.closest ? ev.target.closest('button') : null;
    if (!b) return;
    RANGE = parseInt(b.getAttribute('data-d'), 10) || 0;
    var bs = this.querySelectorAll('button');
    for (var i = 0; i < bs.length; i++) bs[i].className = '';
    b.className = 'on';
    render();
  });

  document.getElementById('segMode').addEventListener('click', function(ev){
    var b = ev.target.closest ? ev.target.closest('button') : null;
    if (!b) return;
    MODE = b.getAttribute('data-m');
    var bs = this.querySelectorAll('button');
    for (var i = 0; i < bs.length; i++) bs[i].className = '';
    b.className = (MODE === 'all' ? 'on g' : 'on');
    render();
  });

  // 拉数据
  function load(){
    fetch('/history.json?t=' + Date.now(), {cache: 'no-store'})
      .then(function(r){ if (!r.ok) throw new Error('no data'); return r.json(); })
      .then(function(d){
        ALL = (d && d.items) || [];
        if (!USER) {
          document.getElementById('sub').textContent = '未登录 · 请先登录账户';
          return;
        }
        document.getElementById('sub').textContent =
          (USER.name || '用户') + ' · 共 ' + ALL.length + ' 次学习记录';
        render();
      })
      .catch(function(){
        document.getElementById('sub').textContent = '数据加载失败，请刷新重试';
      });
  }
  if (USER) load();

  document.getElementById('gateLogin').href =
    '/login?next=' + encodeURIComponent('/history');
})();
</script>
</body>
</html>
"""


def rgb565_to_jpeg_bytes(data, width=320, height=240):
    """RGB565 原始帧 → JPEG 字节流 (设备端不做编码时, 由本中继代做)。

    设备打开 PERCEPTION_RAW_RGB 后直接上传 RGB565, 用于验证
    "崩溃是否与设备端 JPEG 编码有关"。需要 PIL。
    """
    try:
        from PIL import Image
    except ImportError:
        print("[relay] 需要 PIL 处理 RGB565: pip install pillow")
        return data

    n = width * height
    if len(data) < n * 2:
        print("[relay] RGB565 数据不足: %d < %d" % (len(data), n * 2))
        return data

    rgb = bytearray(n * 3)
    for i in range(n):
        p = data[i * 2] | (data[i * 2 + 1] << 8)
        r = (p >> 11) & 0x1F
        g = (p >> 5) & 0x3F
        b = p & 0x1F
        rgb[i * 3]     = (r << 3) | (r >> 2)
        rgb[i * 3 + 1] = (g << 2) | (g >> 4)
        rgb[i * 3 + 2] = (b << 3) | (b >> 2)

    import io
    buf = io.BytesIO()
    Image.frombytes("RGB", (width, height), bytes(rgb)).save(
        buf, format="JPEG", quality=85)
    out = buf.getvalue()
    print("[relay] RGB565 %dKB -> JPEG %dKB" % (len(data) // 1024, len(out) // 1024))
    return out


def generate_advice(stats, mode="strict"):
    """调 MiMo (OpenAI 兼容) 生成中文建议; 失败则用本地模板兜底。"""
    d = stats.get("distractions") or [0, 0, 0, 0]
    want = mode or stats.get("mode", "strict")
    prompt = (REPORT_PROMPT_STRICT if want == "strict"
              else REPORT_PROMPT_GENTLE)
    user_text = (
        "学习数据: 总时长 %s 分钟, 有效学习 %s 分钟, "
        "分心次数 玩手机 %s 次 / 看手机 %s 次 / 离座 %s 次 / 瞌睡 %s 次, "
        "专注度评分 %s 分 (满分100), 模式 %s。"
        % (stats.get("total_min", 0), stats.get("effective_min", 0),
           _at(d, 0), _at(d, 1), _at(d, 2), _at(d, 3),
           stats.get("focus_score", 0),
           "严格" if stats.get("mode") == "strict" else "鼓励")
    )
    body = _mimo_body(MIMO_MODEL, 1024, [
        {"role": "system", "content": "You are MiMo, an AI assistant "
                                      "developed by Xiaomi."},
        {"role": "user", "content": prompt + "\n\n" + user_text},
    ])

    try:
        data = _mimo_post(body, timeout=60)
        content = (data["choices"][0]["message"].get("content") or "").strip()
        if content:
            return content
        print("[relay] MiMo 建议为空 (可能推理占满 token), 用模板兜底")
    except Exception as e:  # noqa: BLE001
        print("[relay] MiMo 建议生成失败: %s" % e)
    return _local_advice(stats, want)


# --------------------------------------------------------------------
# 历史记录演示数据 (无真实后端: 固定 8 条, 时间相对今天递减)
# --------------------------------------------------------------------

HISTORY_SEED_ROWS = [
    # 天前, 时, 分, 总秒, 有效秒, [玩, 看, 离座, 瞌睡], 分数, 模式, 严格, 鼓励
    (1, 20, 14, 2700, 2280, [3, 2, 1, 0], 72, "strict",
     "45 分钟里分心 6 次, 其中玩手机 3 次, 专注度只有 72 分。"
     "手机放在手边就是你最大的干扰源, 下次学习前把它放进包里, 不要给自己留退路。",
     "你今天稳稳学了 45 分钟, 有效时间有 38 分钟, 这已经超过大多数人了。"
     "如果下次能把手机放进抽屉, 你会发现专注其实很轻松。"),
    (2, 19, 40, 3600, 3420, [1, 1, 0, 0], 88, "gentle",
     "这一节 60 分钟只分心 2 次, 专注度 88 分, 说明你完全有能力做到。"
     "但有效时长还差 3 分钟, 别在最后阶段松懈, 收尾也要撑住。",
     "60 分钟里几乎没怎么分心, 专注度 88 分, 这个状态很棒! "
     "下次可以试着把单次学习拉长到 75 分钟, 你会更适应深度状态。"),
    (4, 21, 5, 1800, 1290, [4, 3, 2, 1], 54, "strict",
     "30 分钟里玩手机 4 次、看手机 3 次, 有效学习只有 21 分钟, 专注度 54 分不及格。"
     "你不是不会学, 是根本没坐下来, 先把手机拿出房间再开始。",
     "这次只学了 30 分钟, 你还是坚持下来了, 有效时间也有 21 分钟。"
     "下次学习前先喝口水、把手机放远一点, 状态会好很多。"),
    (6, 16, 30, 4500, 4260, [0, 1, 0, 0], 94, "gentle",
     "75 分钟有效学习 71 分钟, 专注度 94 分, 这是标准答案级别的表现。"
     "唯一要求: 保持住, 不要因为一次优秀就放松标准。",
     "75 分钟里几乎没有分心, 专注度 94 分, 太厉害了! "
     "今天的状态值得记下来, 下次继续用同样的节奏就好。"),
    (9, 20, 0, 2400, 1980, [2, 2, 1, 0], 76, "strict",
     "40 分钟分心 5 次, 有效 33 分钟, 专注度 76 分刚过良好线。"
     "问题在于你每次都只中断十几秒, 但注意力重建要花更久, 要么不碰手机, 要么就别开始。",
     "40 分钟里大部分时间都在学, 专注度 76 分, 已经不错了。"
     "下次遇到想拿手机的冲动, 先在纸上记一笔, 等这一轮结束再处理。"),
    (13, 19, 10, 1500, 960, [5, 4, 2, 1], 48, "strict",
     "25 分钟玩手机 5 次、看手机 4 次, 有效学习只有 16 分钟, 专注度 48 分。"
     "这个数据说明本次学习基本没进入状态, 重新从 15 分钟不带手机开始练。",
     "这次只坚持了 25 分钟, 但你愿意坐下来就已经是进步。"
     "明天试试 15 分钟的小目标, 完成后再奖励自己看手机, 慢慢就顺了。"),
    (16, 15, 0, 3300, 3120, [1, 0, 0, 0], 91, "gentle",
     "55 分钟只分心 1 次, 专注度 91 分, 有效学习 52 分钟。"
     "做得对的地方是不给自己找借口, 继续保持这个强度。",
     "55 分钟里几乎全程在线, 专注度 91 分, 你越来越稳了。"
     "记得中间起来活动一下, 别让身体先累。"),
    (22, 20, 30, 2700, 2160, [2, 3, 1, 1], 68, "strict",
     "45 分钟分心 7 次, 有效 36 分钟, 专注度 68 分。"
     "看手机 3 次、离座和瞌睡各 1 次, 说明你的问题不是手机而是睡眠, 先保证作息再谈专注。",
     "45 分钟学到了 36 分钟, 专注度 68 分, 已经比上次进步了。"
     "如果觉得困, 学习前先小睡 15 分钟, 效果会比硬撑好。"),
]


def demo_history():
    """把 HISTORY_SEED_ROWS 展开成前端要的 items (ts 相对当前时间)。"""
    now = time.time()
    items = []
    for i, row in enumerate(HISTORY_SEED_ROWS):
        days, hh, mm, total, eff, dist, score, mode, adv_s, adv_g = row
        t = time.localtime(now - days * 86400)
        ts = time.mktime((t.tm_year, t.tm_mon, t.tm_mday,
                          hh, mm, 0, 0, 0, -1))
        items.append({
            "id": "demo-%02d" % (i + 1),
            "ts": int(ts),
            "total_sec": total,
            "effective_sec": eff,
            "distractions": dist,
            "focus_score": score,
            "grade": _score_grade(score),
            "mode": mode,
            "advice": adv_s,
            "advice_gentle": adv_g,
            "live": False,
            "demo": True,
        })
    return items


# ---- 真实历史记录 (JSONL 追加) ------------------------------------------
#
# 每完成一局, _handle_report 调 _append_history 追加一行; /history.json
# 调 _load_history 读回来。REPORT_JSON_PATH 只保存"最新一局", 本文件负责
# "多局累积", 因此历史页看到的是真实的历次学习记录。

def _history_entry(report, ts=None):
    """把一份 report 压成历史记录的一行。"""
    st = report.get("stats") or {}
    score = int(st.get("focus_score", 0))
    ach = report.get("achievements") or []
    return {
        "id": "h-%d" % int(ts if ts is not None else time.time()),
        "ts": int(ts if ts is not None else time.time()),
        "total_sec": int(st.get("total_duration_sec", 0)),
        "effective_sec": int(st.get("effective_duration_sec", 0)),
        "distractions": report.get("distraction_by_type") or [0, 0, 0, 0],
        "focus_score": score,
        "grade": report.get("grade") or _score_grade(score),
        "mode": st.get("mode", "strict"),
        "advice": report.get("advice", ""),
        "advice_gentle": report.get("advice_gentle", ""),
        "achievements": sum(1 for a in ach if a.get("unlocked")),
        "live": False,
        "demo": False,
    }


def _append_history(report):
    """把一局摘要追加到历史文件。

    设备侧失败会重发 /report, 因此这里按"内容 + 60 秒内"去重, 避免同一局
    被记成两条。写失败不影响主流程 (报告已单独落盘)。
    """
    try:
        entry = _history_entry(report)
        prev = None
        if os.path.exists(HISTORY_PATH):
            with open(HISTORY_PATH, "r", encoding="utf-8") as f:
                lines = [ln for ln in f if ln.strip()]
            if lines:
                try:
                    prev = json.loads(lines[-1])
                except Exception:  # noqa: BLE001
                    prev = None
            if prev and entry["ts"] - int(prev.get("ts", 0)) <= 60 and \
               prev.get("focus_score") == entry["focus_score"] and \
               prev.get("total_sec") == entry["total_sec"] and \
               list(prev.get("distractions") or []) == list(entry["distractions"]):
                return False
        with open(HISTORY_PATH, "a", encoding="utf-8") as f:
            f.write(json.dumps(entry, ensure_ascii=False) + "\n")
        _trim_history()
        return True
    except Exception as e:  # noqa: BLE001
        print("[relay] history append failed: %s" % e)
        return False


def _trim_history():
    """只保留最近 HISTORY_MAX_ROWS 条。"""
    try:
        with open(HISTORY_PATH, "r", encoding="utf-8") as f:
            lines = [ln for ln in f if ln.strip()]
        if len(lines) <= HISTORY_MAX_ROWS:
            return
        tmp = HISTORY_PATH + ".tmp"
        with open(tmp, "w", encoding="utf-8") as f:
            f.writelines(lines[-HISTORY_MAX_ROWS:])
        os.replace(tmp, HISTORY_PATH)
    except Exception as e:  # noqa: BLE001
        print("[relay] history trim failed: %s" % e)


def _load_history():
    """读回全部历史记录 (坏行跳过)。"""
    items = []
    try:
        with open(HISTORY_PATH, "r", encoding="utf-8") as f:
            for ln in f:
                ln = ln.strip()
                if not ln:
                    continue
                try:
                    items.append(json.loads(ln))
                except Exception:  # noqa: BLE001
                    continue
    except FileNotFoundError:
        pass
    except Exception as e:  # noqa: BLE001
        print("[relay] history load failed: %s" % e)
    return items


class RelayHandler(http.server.BaseHTTPRequestHandler):
    # HTTP/1.1 —— 设备端复用同一条 TCP 连接上传每一帧 (约 5s 一帧)。
    # HTTP/1.0 会在每个响应后强制关闭连接, 设备就得每帧重新 connect/close,
    # 正是这个churn把 nuttx 的 TCP/IOB 池耗尽, connect 失败返回 -20
    # (FOCUS_ERR_NET_DISCONN)。
    protocol_version = "HTTP/1.1"
    # 空闲连接回收: 设备退回 IDLE 后连接会闲置, 别让服务端线程永久阻塞
    timeout = 120

    def do_POST(self):
        try:
            # 1. 先把请求体读干净。必须在任何提前 return 之前读完 ——
            #    keep-alive 连接上残留的 body 会被当成下一个请求的起始行。
            length = int(self.headers.get("Content-Length", 0))
            body = self.rfile.read(length)

            path = self.path.split("?")[0]

            # 1.5 网页登录 (演示): 网页用户没有设备令牌, 故放在令牌校验之前
            if path == "/api/login":
                return self._handle_login(body)

            # 2. 校验中继令牌 (设备侧 wifi_set_http_auth 设置)
            if RELAY_TOKEN:
                auth = self.headers.get("Authorization", "")
                if auth != "Bearer " + RELAY_TOKEN:
                    self._reply(403, b'{"error":"forbidden: bad relay token"}')
                    return

            # 2.1 学习报告: 设备一局结束上传统计 → MiMo 生成建议 → 存网页
            #     (路径用 /report; 其余路径一律按识图处理)
            if path == "/report":
                return self._handle_report(body)

            # 2.5 顺带存最新一帧供网页预览 (失败不影响识别转发)
            self._save_preview_frame(body)

            # 3. 识别: 优先本机检测服务 (DETECT_URL), 否则 MiMo 云端
            if DETECT_URL:
                mime, rw, rh, jpeg = self._extract_image(body)
                if jpeg is None:
                    self._reply(400, b'{"error":"no image in request body"}')
                    return
                if mime == "x-rgb565":
                    jpeg = rgb565_to_jpeg_bytes(base64.b64decode(jpeg), rw, rh)
                else:
                    jpeg = base64.b64decode(jpeg)   # 设备直传 JPEG 时也解码
                result = self._detect_local(jpeg)
                payload = self._to_openai_response(result)
                self._reply(200, payload)
            else:
                # 直连 MiMo 的兜底路径 (未配置 DETECT_URL 时)。
                # 设备请求体里的 model 可能是已下线的旧名字 (如 mimo-v2.5),
                # 统一以中继的 MIMO_MODEL 为准, 并套用思考开关。
                try:
                    fwd = json.loads(body.decode("utf-8", "replace"))
                    if not isinstance(fwd, dict):
                        raise ValueError("body is not an object")
                except Exception:  # noqa: BLE001
                    fwd = {"messages": []}
                fwd["model"] = MIMO_MODEL
                if MIMO_THINKING == "disabled":
                    fwd["thinking"] = {"type": "disabled"}
                try:
                    data = _mimo_post(fwd, timeout=90)
                    self._reply(200, json.dumps(data, ensure_ascii=False)
                                .encode("utf-8"))
                except urllib.error.HTTPError as e:
                    self._reply(e.code, e.read())

        except Exception as e:  # noqa: BLE001
            self._reply(502, ("{\"error\":\"relay: %s\"}" % e).encode())

    def _handle_login(self, body):
        """演示用登录: 不校验密码 (无真实后端), 只回一个本地会话标识。

        生产环境应在此接入真实鉴权 (校验账号密码 / 下发 JWT 或 cookie),
        并把 /history 与 /history.json 的访问纳入登录态校验。
        """
        try:
            data = json.loads(body.decode("utf-8", "replace") or "{}")
        except Exception:  # noqa: BLE001
            data = {}
        account = (data.get("account") or "").strip()
        if not account:
            return self._reply(400,
                               b'{"ok":false,"error":"\\u8bf7\\u8f93\\u5165\\u624b\\u673a\\u53f7\\u6216\\u90ae\\u7bb1"}')

        # 展示名: 邮箱取 @ 前, 手机号取后 4 位
        if "@" in account:
            name = account.split("@")[0] or "用户"
        elif len(account) >= 4:
            name = "用户" + account[-4:]
        else:
            name = account or "用户"

        payload = json.dumps({
            "ok": True,
            "user": {
                "name": name,
                "account": account,
                "token": uuid.uuid4().hex,
                "ts": int(time.time()),
            },
        }, ensure_ascii=False).encode()
        self._reply(200, payload)

    def _handle_report(self, body):
        """设备上传学习统计 → 调 MiMo 生成建议 → 存 /tmp/report.json 供网页。

        设备请求体 (见 ui/mimo.c):
          {"total_min":N,"effective_min":N,"distractions":[a,b,c,d],
           "focus_score":N,"mode":"strict|gentle"}
        """
        try:
            stats = json.loads(body.decode("utf-8", "replace"))
        except Exception as e:  # noqa: BLE001
            return self._reply(400, ("{\"error\":\"bad report json: %s\"}" % e)
                               .encode())

        # 两种模式各生成一份建议 (网页上并列展示, 便于对比)
        advice         = generate_advice(stats, "strict")
        advice_gentle  = generate_advice(stats, "gentle")
        report = {
            "stats": {
                # 网页统一用秒, 设备上报的是分钟
                "total_duration_sec": int(stats.get("total_min", 0)) * 60,
                "effective_duration_sec": int(stats.get("effective_min", 0)) * 60,
                "distraction_count": sum(stats.get("distractions") or [0, 0, 0, 0]),
                "focus_score": int(stats.get("focus_score", 0)),
                "current_mode": 0 if stats.get("mode") == "strict" else 1,
                "mode": stats.get("mode", "strict"),
            },
            "distraction_by_type": stats.get("distractions") or [0, 0, 0, 0],
            "advice": advice,
            "advice_gentle": advice_gentle,
            "grade": _score_grade(int(stats.get("focus_score", 0))),
            "achievements": compute_achievements(
                stats, stats.get("distractions") or [0, 0, 0, 0]),
            "generated_at": time.strftime("%Y-%m-%d %H:%M:%S"),
        }

        try:
            tmp = REPORT_JSON_PATH + ".tmp"
            with open(tmp, "w", encoding="utf-8") as f:
                json.dump(report, f, ensure_ascii=False)
            os.replace(tmp, REPORT_JSON_PATH)
            print("[relay] report saved (score=%s, advice %d chars)"
                  % (report["stats"]["focus_score"], len(advice)))
        except Exception as e:  # noqa: BLE001
            print("[relay] report save failed: %s" % e)

        # 追加到历史记录 (与"最新一局"分开存, 用于 /history 的累积视图)
        if _append_history(report):
            print("[relay] history +1 (score=%s)"
                  % report["stats"]["focus_score"])

        self._reply(200, json.dumps({"ok": True, "advice": advice},
                                    ensure_ascii=False).encode())

    def _detect_local(self, jpeg):
        """把 JPEG 以 multipart/form-data 发给本机检测服务, 返回解析后的 JSON。

        本机服务契约 (见《手机使用检测服务-使用说明.md》):
          POST <DETECT_URL>  -F "file=@frame.jpg"
          → {using_phone, has_person, confidence, reason,
             phone:{bbox}, hand:{bbox}, persons:[{bbox}], img_shape:[h,w]}
        """
        boundary = "----focusaiotrelayboundary"
        parts = []
        parts.append(("--%s\r\n" % boundary).encode())
        parts.append(b'Content-Disposition: form-data; name="file"; '
                     b'filename="frame.jpg"\r\n')
        parts.append(b"Content-Type: image/jpeg\r\n\r\n")
        parts.append(jpeg)
        parts.append(("\r\n--%s--\r\n" % boundary).encode())
        payload = b"".join(parts)

        req = urllib.request.Request(
            DETECT_URL,
            data=payload,
            headers={"Content-Type":
                     "multipart/form-data; boundary=" + boundary},
        )
        with urllib.request.urlopen(req, timeout=DETECT_TIMEOUT) as resp:
            return json.loads(resp.read().decode("utf-8", "replace"))

    @staticmethod
    def _to_openai_response(result):
        """本机检测结果 → 设备期望的 OpenAI chat/completions 格式。

        设备 perception.c 从 choices[0].message.content 里取 JSON, 字段:
          person_present / person_bbox[x,y,w,h 归一化] / phone_detected /
          phone_near_hand / head_pitch / head_yaw / hand_motion_score / confidence

        本机模型没有头部姿态/手部运动量。
        头部姿态置 0; hand_motion_score 用作"玩手机"开关: 行为引擎要求
        phone_motion > 0.3 才判 PLAYING_PHONE, 否则只判 GLANCING_PHONE。
        客户要求"看手机也归为玩手机"(一刀切), 故 using_phone=true 时置 1.0。
        """
        persons = result.get("persons") or []
        img_shape = result.get("img_shape") or [0, 0]
        img_h = float(img_shape[0]) or 0.0
        img_w = float(img_shape[1]) or 0.0

        # 人物框: 取第一个 person, 像素 [x1,y1,x2,y2] → 归一化 [x,y,w,h]
        bbox_norm = [0.0, 0.0, 0.0, 0.0]
        if persons and img_w > 0 and img_h > 0:
            bb = persons[0].get("bbox") or []
            if len(bb) >= 4:
                x1, y1, x2, y2 = (float(bb[0]), float(bb[1]),
                                  float(bb[2]), float(bb[3]))
                bbox_norm = [
                    max(0.0, min(1.0, x1 / img_w)),
                    max(0.0, min(1.0, y1 / img_h)),
                    max(0.0, min(1.0, (x2 - x1) / img_w)),
                    max(0.0, min(1.0, (y2 - y1) / img_h)),
                ]

        using_phone = bool(result.get("using_phone"))

        observation = {
            "person_present": bool(result.get("has_person")),
            "person_bbox": bbox_norm,
            "phone_detected": bool(result.get("phone")),
            "phone_near_hand": using_phone,
            "head_pitch": 0.0,
            "head_yaw": 0.0,
            # 一刀切: 判定使用手机即视为"玩手机" (行为引擎要求 >0.3)
            "hand_motion_score": 1.0 if using_phone else 0.0,
            "confidence": float(result.get("confidence") or 0.0),
        }

        # 包成 OpenAI chat/completions 响应 (content 是 JSON 字符串)
        envelope = {
            "choices": [{
                "index": 0,
                "finish_reason": "stop",
                "message": {
                    "role": "assistant",
                    "content": json.dumps(observation, ensure_ascii=False),
                },
            }],
        }
        return json.dumps(envelope, ensure_ascii=False).encode("utf-8")

    def do_GET(self):
        # 网页路由 (POST 转发不受影响)。路径可能带 ?t= 缓存参数, 故按前缀匹配。
        path = self.path.split("?")[0]
        if path == "/preview":
            self._reply(200, _with_nav(PREVIEW_PAGE),
                        content_type="text/html; charset=utf-8")
        elif path == "/preview.jpg":
            self._serve_preview_jpeg()
        elif path == "/report":
            self._reply(200, _with_nav(REPORT_PAGE),
                        content_type="text/html; charset=utf-8")
        elif path == "/report.json":
            self._serve_report_json()
        elif path == "/login":
            self._reply(200, _with_nav(LOGIN_PAGE),
                        content_type="text/html; charset=utf-8")
        elif path == "/history":
            self._reply(200, _with_nav(HISTORY_PAGE),
                        content_type="text/html; charset=utf-8")
        elif path == "/history.json":
            self._serve_history_json()
        elif path == "/":
            # 根路径直接进预览页, 保持原有访问习惯
            self._reply(302, b"", extra_headers={"Location": "/preview"})
        else:
            self._reply(404, b'{"error":"not found"}')

    def _serve_history_json(self):
        """历史记录: 返回真实累积的学习记录, 按时间倒序。

        数据来源是 HISTORY_PATH (JSONL), 每完成一局由 _append_history 追加一行。
        历史为空时 (首次部署 / 还没跑过一局), 用固定的演示行兜底, 保证页面不空;
        一旦有真实记录, 就只展示真实数据 —— 不把演示数据混进真实历史里。

        想让演示重新显示固定数据: 清空历史文件, 或设 HISTORY_SEED_DEMO=0 关闭兜底。
        """
        items = _load_history()
        if not items and HISTORY_SEED_DEMO:
            items = demo_history()
        items.sort(key=lambda x: x.get("ts", 0), reverse=True)
        payload = json.dumps({"ok": True, "items": items},
                             ensure_ascii=False).encode()
        self._reply(200, payload, extra_headers={"Cache-Control": "no-store"})

    def _serve_report_json(self):
        try:
            with open(REPORT_JSON_PATH, "rb") as f:
                payload = f.read()
            self._reply(200, payload,
                        extra_headers={"Cache-Control": "no-store"})
        except FileNotFoundError:
            self._reply(404, b'{"error":"no report yet"}')
        except OSError as e:
            self._reply(500, ("{\"error\":\"report io: %s\"}" % e).encode())

    def _serve_preview_jpeg(self):
        try:
            with open(PREVIEW_JPEG_PATH, "rb") as f:
                payload = f.read()
            self._reply(200, payload, content_type="image/jpeg",
                        extra_headers={"Cache-Control": "no-store"})
        except FileNotFoundError:
            self._reply(404, b'{"error":"no preview frame yet"}')
        except OSError as e:
            self._reply(500, ("{\"error\":\"preview io: %s\"}" % e).encode())

    def _save_preview_frame(self, body):
        try:
            mime, rw, rh, b64 = self._extract_image(body)
            if b64 is None:
                return
            data = base64.b64decode(b64)
            if mime == "x-rgb565":
                data = rgb565_to_jpeg_bytes(data, rw, rh)
            if len(data) < 4:  # 空/损坏帧直接丢弃
                return
            tmp = PREVIEW_JPEG_PATH + ".tmp"
            with open(tmp, "wb") as f:
                f.write(data)
            os.replace(tmp, PREVIEW_JPEG_PATH)  # 原子替换, 网页不会读到半张图
            print("[relay] preview saved %d bytes" % len(data))
        except Exception as e:  # noqa: BLE001
            print("[relay] preview save failed: %s" % e)

    @staticmethod
    def _extract_image(body):
        """从请求体提取 (mime, w, h, b64)。支持 jpeg 与 x-rgb565 两种。

        设备端打开 PERCEPTION_RAW_RGB 时发
        data:image/x-rgb565[-<w>x<h>];base64,<b64> —— 设备不做 JPEG 编码,
        由本中继转码。

        按 "data:image/" 前缀通配解析, 分辨率从 mime 里读、不写死: 设备
        改分辨率时中继无需跟着改 (例如从 160x120 回到 320x240)。
        """
        idx = body.find(b"data:image/")
        if idx < 0:
            return None, 0, 0, None
        start = idx + len(b"data:image/")
        end = body.find(b'"', start)
        if end < 0:
            end = len(body)
        header = body[start:end].decode("ascii", "replace")

        # header 形如 "x-rgb565-320x240;base64,<b64>" 或 "jpeg;base64,<b64>"
        parts = header.split(";", 1)
        if len(parts) != 2:
            return None, 0, 0, None
        tag, rest = parts[0], parts[1]
        if not rest.startswith("base64,"):
            return None, 0, 0, None

        w, h = 320, 240
        if tag.startswith("x-rgb565"):
            suffix = tag[len("x-rgb565"):]           # "" 或 "-320x240"
            if suffix.startswith("-"):
                try:
                    dim = suffix[1:].split("x")
                    w, h = int(dim[0]), int(dim[1])
                except Exception:  # noqa: BLE001
                    pass
            tag = "x-rgb565"                          # 归一化, 调用方按此分支
        else:
            tag = "jpeg"
        return tag, w, h, rest[len("base64,"):]

    @staticmethod
    def _extract_jpeg_b64(body):
        """从 OpenAI 兼容请求体提取 data:image/jpeg;base64,<b64>。
        base64 只含 A-Za-z0-9+/= (不含引号), 读到下一个引号即可, 无需 JSON 库。"""
        marker = b"data:image/jpeg;base64,"
        idx = body.find(marker)
        if idx < 0:
            return None
        start = idx + len(marker)
        end = body.find(b'"', start)
        if end < 0:
            end = len(body)
        return body[start:end]

    def _extract_jpeg(self, body):
        """提取并解码出 JPEG 二进制 (供本机检测服务转发)。"""
        b64 = self._extract_jpeg_b64(body)
        if b64 is None:
            return None
        try:
            data = base64.b64decode(b64)
        except Exception:  # noqa: BLE001
            return None
        return data if len(data) >= 4 else None

    def _reply(self, code, payload, content_type="application/json",
               extra_headers=None):
        self.send_response(code)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(payload)))
        for k, v in (extra_headers or {}).items():
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(payload)

    def log_message(self, fmt, *args):  # 静音
        sys.stderr.write("[relay] %s\n" % (fmt % args))


def _with_nav(page):
    """把顶部导航注入页面 (只在第一个 <body> 后插入, 不改动原有结构)。"""
    return page.replace("<body>", "<body>" + NAV_HTML, 1).encode("utf-8")


# 是否在无报告时预置演示数据 (设 0 关闭)
SEED_DEMO_REPORT = os.environ.get("SEED_DEMO_REPORT", "1") != "0"

# 预置演示数据的两个档位:
#   normal —— 常规一局 (45 分钟 / 72 分 / 有分心), 解锁 4 项成就, 贴近真实使用
#   hero   —— 高光一局 (65 分钟 / 93 分 / 几乎零分心), 解锁 8 项成就,
#             专门用于现场演示"成就系统"
# 切换: DEMO_PROFILE=hero ./mimo_relay.py
DEMO_PROFILES = {
    "normal": {
        "total_min": 45, "effective_min": 38,
        "distractions": [3, 2, 1, 0],          # 玩/看/离座/瞌睡
        "focus_score": 72, "mode": "strict",
    },
    "hero": {
        "total_min": 65, "effective_min": 62,
        "distractions": [0, 1, 0, 0],
        "focus_score": 93, "mode": "strict",
    },
}
DEMO_PROFILE = os.environ.get("DEMO_PROFILE", "normal")
DEMO_STATS = DEMO_PROFILES.get(DEMO_PROFILE, DEMO_PROFILES["normal"])


def _seed_demo_report():
    """用预设数据生成两套建议并落盘, 供网页演示 (无需设备上传)。"""
    stats = DEMO_STATS
    report = {
        "stats": {
            "total_duration_sec": stats["total_min"] * 60,
            "effective_duration_sec": stats["effective_min"] * 60,
            "distraction_count": sum(stats["distractions"]),
            "focus_score": stats["focus_score"],
            "current_mode": 0,
            "mode": stats["mode"],
        },
        "distraction_by_type": stats["distractions"],
        "advice": generate_advice(stats, "strict"),
        "advice_gentle": generate_advice(stats, "gentle"),
        "grade": _score_grade(stats["focus_score"]),
        "achievements": compute_achievements(stats, stats["distractions"]),
        "generated_at": time.strftime("%Y-%m-%d %H:%M:%S") + " (演示数据)",
    }
    tmp = REPORT_JSON_PATH + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(report, f, ensure_ascii=False)
    os.replace(tmp, REPORT_JSON_PATH)
    got = sum(1 for a in report["achievements"] if a["unlocked"])
    print("[relay] 已预置演示报告 (profile=%s): 成就 %d/%d, 严格 %d 字 / 鼓励 %d 字"
          % (DEMO_PROFILE, got, len(report["achievements"]),
             len(report["advice"]), len(report["advice_gentle"])))


def main():
    # 输出重定向到文件时 Python 默认块缓冲, 启动配置/请求日志会迟迟不落盘。
    # 改为行缓冲, 便于 tail -f 实时观察。
    try:
        sys.stdout.reconfigure(line_buffering=True)
    except Exception:  # noqa: BLE001
        pass

    print("[relay] 中继监听 0.0.0.0:%d" % PORT)
    print("[relay]   识图: %s" % (DETECT_URL if DETECT_URL
                                  else "MiMo 云端 " + MIMO_URL))
    print("[relay]   建议: MiMo %s (模型 %s)" % (MIMO_URL, MIMO_MODEL))
    print("[relay]   网页: http://<本机IP>:%d/preview  (实时画面)" % PORT)
    print("[relay]         http://<本机IP>:%d/report   (学习报告)" % PORT)
    print("[relay]         http://<本机IP>:%d/login    (登录, 演示鉴权)" % PORT)
    print("[relay]         http://<本机IP>:%d/history  (历史记录, 演示数据)" % PORT)
    if not MIMO_API_KEY:
        print("[relay] 警告: 未设置 MIMO_API_KEY, 学习建议将用本地模板兜底")
    if RELAY_TOKEN:
        print("[relay] 已启用访问令牌校验 (设备需带 Authorization)")
    else:
        print("[relay] 警告: 未设置 RELAY_TOKEN, 不校验来源")

    # 预置演示报告: 设备未上传时网页也有内容 (评委打开就能看到).
    # 删除 REPORT_JSON_PATH 即可重新生成; 设备真实上报后会被覆盖。
    if SEED_DEMO_REPORT and not os.path.exists(REPORT_JSON_PATH):
        try:
            _seed_demo_report()
        except Exception as e:  # noqa: BLE001
            print("[relay] 预置演示报告失败: %s" % e)

    # 必须用多线程: 单线程 HTTPServer 在 MiMo 慢请求时无法 accept 新连接,
    # 内核接收队列积压满后直接丢弃 SYN (真机表现为连接超时 / 网络不通)。
    http.server.ThreadingHTTPServer(("0.0.0.0", PORT), RelayHandler).serve_forever()


if __name__ == "__main__":
    main()
