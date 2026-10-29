// Firmware-local dashboard. No CDN, telemetry, or remote QR service.
document.documentElement.lang = 'zh-Hant';
document.title = '空調遙控器 · BC7215A';
const viewport = document.createElement('meta');
viewport.name = 'viewport'; viewport.content = 'width=device-width,initial-scale=1';
document.head.append(viewport);
const app = document.querySelector('esp-app');
app.innerHTML = `<main>
 <header><div class="brand"><div class="mark"><svg viewBox="0 0 28 28" fill="none" stroke="currentColor" stroke-width="1.6"><path d="M3 9h16c6 0 6-7 1-7M3 14h20c5 0 5 7 0 7M3 19h10c5 0 5 7 1 7"/></svg></div><div><h1>空調遙控器</h1><div class="eyebrow">BC7215A · HOME CLIMATE</div></div></div><nav aria-label="頁面導覽"><a href="#control">空調控制</a><a href="#pairing">遙控器配對</a><a href="#wifi">Wi-Fi</a><a href="#matter">Matter</a></nav></header>
 <section class="intro"><div><h2>讓舒適，剛剛好。</h2><p>控制空調、查看室內環境，在這裡一次完成。</p></div><span id="connection" class="badge offline" role="status">連線中</span></section>
 <div class="disconnected-note">裝置連線中斷，畫面保留最後資料；重新連線後會自動更新。</div>
 <section class="stats" aria-label="室內環境"><div class="stat"><div class="label">室內溫度</div><strong><span id="temperature">—</span><small>°C</small></strong></div><div class="stat"><div class="label">相對濕度</div><strong><span id="humidity">—</span><small>%</small></strong></div><div class="stat network"><div class="label">目前網路</div><strong id="network-name">讀取中</strong><div id="network-address" class="sub">等待裝置資訊</div><a id="local-address" class="sub" hidden></a></div></section>
 <div class="grid"><section id="control" class="card climate"><div class="card-head"><h3>空調控制</h3><span id="mode-label" class="badge">等待狀態</span></div><div class="dial"><div class="label">設定溫度</div><div class="setpoint"><button class="round" id="temp-down" aria-label="降低一度" disabled>−</button><strong id="setpoint">—</strong><span class="unit">°C</span><button class="round" id="temp-up" aria-label="提高一度" disabled>＋</button></div><div id="known" class="label">請先完成遙控器配對</div></div><div id="modes" class="mode-row" aria-label="空調模式"></div><div class="control-row"><label for="fan">風速</label><select id="fan" disabled><option value="Auto">自動</option><option value="Low">低速</option><option value="Medium">中速</option><option value="High">高速</option></select></div><p class="footnote">依紅外指令與遙控訊號更新，實際運轉請以冷氣為準。</p></section>
 <section id="pairing" class="card"><div class="card-head"><h3>遙控器配對</h3><span class="index">REMOTE SETUP</span></div><div id="status" class="status-box" role="status" aria-live="polite">等待裝置狀態…</div><div class="detail-line"><span>目前匹配</span><strong id="match">—</strong></div><div class="buttons"><button class="primary" data-button="Pair" disabled>開始配對</button><button data-button="NextMatch" disabled>下一個匹配</button><button data-button="CancelPair" disabled>取消配對</button></div><p class="hint">配對時，將原廠遙控器設為冷房 25°C，對準接收頭按風速鍵。</p></section>
 <section id="wifi" class="card"><div class="card-head"><h3>Wi-Fi 連線</h3><span class="index">NETWORK</span></div><p class="hint">選擇家中的 2.4 GHz 網路，也可以直接輸入名稱。</p><form id="wifi-form"><label for="wifi-list">找到的網路</label><div class="field-row"><select id="wifi-list"><option value="">讀取網路清單…</option></select><button id="wifi-refresh" type="button">重新讀取</button></div><label for="ssid">Wi-Fi 名稱</label><input id="ssid" name="ssid" autocomplete="off" placeholder="輸入完整的 Wi-Fi 名稱" required maxlength="32"><label for="psk">Wi-Fi 密碼</label><div class="field-row"><input id="psk" name="psk" type="password" autocomplete="new-password" placeholder="輸入密碼；開放網路可留空" maxlength="64"><button id="show-password" type="button" aria-label="顯示密碼">顯示</button></div><div class="buttons"><button class="primary" id="wifi-save" type="submit">儲存並連線</button></div><div id="wifi-message" class="form-message" role="status"></div></form><p class="hint">更換網路時頁面可能暫時離線。連線完成後，請使用裝置的新 IP 開啟此頁。</p></section>
 <section id="matter" class="card"><div class="card-head"><h3>加入 Matter 家庭</h3><span class="index">CONNECTED HOME</span></div><div id="matter-status" class="status-box" role="status">讀取配對視窗狀態…</div><div class="buttons"><button data-button="MatterPair" disabled>開放配對 15 分鐘</button><button class="danger" data-button="ClearMatterFabrics" disabled>清除 Matter 家庭配對</button></div><p id="matter-joined-note" class="hint" hidden>已加入家庭，配對碼已隱藏。需要新增家庭時，按「開放配對 15 分鐘」，或從原家庭 App 分享。</p><div class="matter-content qr-layout" hidden><div id="qr" class="qr-wrap" aria-label="Matter 配對 QR 碼">等待配對碼</div><div class="matter-copy"><h4>掃描即可開始配對</h4><p>尚未加入家庭時會自動開放配對。先讓裝置與家庭中樞連上同一網路，再從支援 Matter 的 App 加入裝置。</p><div class="label">手動配對碼</div><div id="manual-code" class="manual-code">—</div><button id="copy-code" disabled>複製配對碼</button></div></div><details class="qr-extra" hidden><summary>查看 QR 碼內容</summary><code id="qr-text">等待裝置提供</code></details><p class="footnote">空調包含風速調整能力；App 是否顯示控制項目取決於平台支援。QR 碼於此頁離線產生。</p></section></div>
 <details class="advanced"><summary>裝置資訊與維護</summary><div id="diagnostics" class="diagnostics"></div><h4>裝置實體與維護</h4><div id="all-entities" class="diagnostics"></div><div class="diag-actions"><button data-button="RecoverReception" disabled>重整紅外接收</button><button data-button="RetryCommunication" disabled>重新檢查通訊</button><button data-button="Restart" disabled>重新啟動</button></div></details>
 <footer><span>BC7215A · ESPHome + Matter</span><span>v1.0.33 · 本機控制</span></footer></main><div id="toast" class="toast" role="status"></div>`;
const $ = s => document.querySelector(s);
const entities = new Map();
let connected = false, lastQR = '', toastTimer, pendingTemperature = null;
const modeNames = {off:'關機',cool:'冷房',heat:'暖房',auto:'自動',dry:'除濕',fan_only:'送風'};
const modeIcons = {off:'⏻',cool:'❄',heat:'☀',auto:'↻',dry:'♧',fan_only:'≋'};
for (const [key,label] of Object.entries(modeNames)) {
  const btn=document.createElement('button');btn.dataset.mode=key;btn.disabled=true;
  btn.innerHTML=`<span aria-hidden="true">${modeIcons[key]}</span>${label}`;
  btn.onclick=()=>climateCommand({mode:key});$('#modes').append(btn);
}
function domainOf(e){return e.domain || e.id.split(/[/-]/,1)[0];}
function find(suffix,domain) {
  return [...entities.values()].find(e=>(!domain||domainOf(e)===domain)&&
    ((e.name||'').endsWith('_'+suffix)||e.id.toLowerCase().endsWith('_'+suffix.toLowerCase())));
}
function textValue(suffix,domain='text_sensor'){const e=find(suffix,domain);return e?.state??'—';}
function enabled(suffix){const e=find(suffix,'binary_sensor');return e?.value===true||e?.value===1||String(e?.state).toLowerCase()==='on';}
function climate(){return [...entities.values()].find(e=>domainOf(e)==='climate');}
function numeric(v,digits=1){const n=parseFloat(v);return Number.isFinite(n)?n.toFixed(digits):'—';}
function toast(message){$('#toast').textContent=message;clearTimeout(toastTimer);toastTimer=setTimeout(()=>$('#toast').textContent='',4500);}
function path(e){const i=e.id.includes('/')?e.id.indexOf('/'):e.id.indexOf('-');return '/'+e.id.slice(0,i)+'/'+encodeURIComponent(e.id.slice(i+1));}
async function send(e,action,params={}){
  if(!connected||!e){toast('裝置尚未連線，請稍後再試');return false;}
  try{const query=new URLSearchParams(params);const r=await fetch(path(e)+'/'+action+(query.size?'?'+query:''),{method:'POST',signal:AbortSignal.timeout(8000)});if(!r.ok)throw Error();return true;}
  catch{toast('指令未成功送達，請確認連線後再試');return false;}
}
async function climateCommand(params){if(await send(climate(),'set',params))toast('指令已送達，等待裝置狀態更新');}
$('#temp-down').onclick=()=>changeTemp(-1);$('#temp-up').onclick=()=>changeTemp(1);
function changeTemp(delta){const c=climate();if(!c)return;const base=pendingTemperature??parseFloat(c.target_temperature);if(!Number.isFinite(base))return;pendingTemperature=Math.max(16,Math.min(30,base+delta));$('#setpoint').textContent=pendingTemperature;const target=pendingTemperature;climateCommand({target_temperature:target});setTimeout(()=>{if(pendingTemperature===target){pendingTemperature=null;render();}},3000);}
$('#fan').onchange=e=>climateCommand({fan_mode:e.target.value});
function confirmAction(suffix){
  if(suffix==='ClearMatterFabrics')return confirm('清除所有 Matter 家庭配對？Apple 家庭等控制器將無法繼續控制，需要重新加入。Wi-Fi 與冷氣配對會保留。');
  return suffix!=='Restart'||confirm('重新啟動空調遙控器？');
}
document.querySelectorAll('[data-button]').forEach(btn=>btn.onclick=async()=>{
  if(!confirmAction(btn.dataset.button))return;
  if(await send(find(btn.dataset.button,'button'),'press'))toast('操作已送達，請查看遙控器配對狀態');
});
function setConnection(value){connected=value;$('#connection').textContent=value?'裝置已連線':'連線中斷';$('#connection').classList.toggle('offline',!value);document.body.classList.toggle('is-offline',!value);render();}
function render(){
  const c=climate(),paired=enabled('Paired'),status=textValue('Status');
  $('#temperature').textContent=numeric(find('Temperature','sensor')?.value??c?.current_temperature);
  $('#humidity').textContent=numeric(find('Humidity','sensor')?.value??c?.current_humidity);
  $('#network-name').textContent=textValue('SSID')==='—'?'備用 AP／等待連線':textValue('SSID');
  $('#network-address').textContent=([...entities.values()].find(e=>domainOf(e)==='text_sensor'&&(e.name||'').startsWith('IP位址'))?.state)||location.hostname;
  $('#status').textContent=status==='—'?'等待裝置狀態…':status;$('#match').textContent=textValue('Match');
  $('#known').textContent=!paired?'請先完成遙控器配對':enabled('StateKnown')?'已同步最後有效狀態':'狀態尚未確認；請觀察冷氣或用原廠遙控器同步';
  const mode=String(c?.mode||'').toLowerCase();$('#mode-label').textContent=modeNames[mode]||'等待狀態';
  if(pendingTemperature===null)$('#setpoint').textContent=numeric(c?.target_temperature,0);
  $('#temp-down').disabled=$('#temp-up').disabled=$('#fan').disabled=!connected||!paired||!c;
  document.querySelectorAll('[data-mode]').forEach(b=>{b.disabled=!connected||!paired||!c;b.classList.toggle('selected',b.dataset.mode===mode);b.setAttribute('aria-pressed',b.dataset.mode===mode);});
  if(c?.fan_mode){const option=[...$('#fan').options].find(o=>o.value.toLowerCase()===String(c.fan_mode).toLowerCase());if(option)$('#fan').value=option.value;}
  document.querySelectorAll('[data-button]').forEach(b=>b.disabled=!connected||!find(b.dataset.button,'button'));
  const qrEntity=[...entities.values()].find(e=>(e.name||'').includes('Matter配對碼')||e.id.includes('matter_pei_dui_ma'));
  const manual=[...entities.values()].find(e=>(e.name||'').includes('Matter手動配對碼')||e.id.includes('matter_shou_dong_pei_dui_ma'));
  const matterStatus=textValue('MatterStatus');
  const fabricMatch=matterStatus.match(/配對紀錄\s*(\d+)\s*組/);
  const fabricCount=fabricMatch?Number(fabricMatch[1]):null;
  const showCodes=fabricCount===0 || (fabricCount!==null && matterStatus.includes("手動15分鐘"));
  $('#matter-status').textContent=matterStatus==='—'?'正在讀取 Matter 狀態；未配對時將自動開放掃碼':matterStatus;
  $('#matter .qr-layout').hidden=!showCodes;
  $('#matter .qr-extra').hidden=!showCodes;
  $('#matter-joined-note').hidden=!(fabricCount>0)||showCodes;
  $('#matter [data-button="MatterPair"]').hidden=false;
  const code=showCodes?String(qrEntity?.state||''):'';$('#qr-text').textContent=code||'等待裝置提供';
  $('#manual-code').textContent=showCodes?(manual?.state||'—'):'—';$('#copy-code').disabled=!showCodes||!/^\d{11}$|^\d{21}$/.test((manual?.state||'').replace(/[-\s]/g,''));
  if(!showCodes){$('#qr').replaceChildren();lastQR='';}
  if(code.startsWith('MT:')&&code!==lastQR){try{drawQR(code);lastQR=code;}catch{$('#qr').textContent='QR 碼產生失敗，請使用手動碼';}}
  renderAllEntities();
  const names=[['PairingID','配對資料識別碼'],['LibraryVersion','碼庫版本'],['BSSID','Wi-Fi 基地台']];
  const diagnostics=$('#diagnostics');diagnostics.replaceChildren();
  for(const [suffix,label] of names){const row=document.createElement('div');row.className='detail-line';const l=document.createElement('span'),v=document.createElement('strong');l.textContent=label;v.textContent=textValue(suffix);row.append(l,v);diagnostics.append(row);}
}
function renderAllEntities(){
  const root=$('#all-entities');
  for(const e of entities.values()){
    if((e.name||'').includes('Matter配對碼')||(e.name||'').includes('Matter手動配對碼'))continue; // Codes live only in the conditional Matter card.
    if(domainOf(e)==='light')continue; // Internal pairing LED is not a user control.
    let row=[...root.children].find(r=>r.dataset.entity===e.id);
    if(!row){row=document.createElement('div');row.className='detail-line';row.dataset.entity=e.id;
      const label=document.createElement('span');label.textContent=e.name||e.id;row.append(label);
      const control=document.createElement(domainOf(e)==='button'?'button':'strong');row.append(control);root.append(row);
      if(domainOf(e)==='button'){control.textContent='執行';control.onclick=async()=>{if(!confirmAction((e.name||'').split('_').pop()))return;if(await send(e,'press'))toast('操作已送達，請查看運作狀態');};}
    }
    if(domainOf(e)==='button')row.lastChild.disabled=!connected;
    else row.lastChild.textContent=e.state??'—';
  }
}
function drawQR(text){const q=qrcodegen.QrCode.encodeText(text,qrcodegen.QrCode.Ecc.MEDIUM);const scale=6,border=4;const canvas=document.createElement('canvas');canvas.width=canvas.height=(q.size+border*2)*scale;canvas.setAttribute('role','img');canvas.setAttribute('aria-label','Matter 配對 QR 碼');const ctx=canvas.getContext('2d');ctx.fillStyle='#fff';ctx.fillRect(0,0,canvas.width,canvas.height);ctx.fillStyle='#000';for(let y=0;y<q.size;y++)for(let x=0;x<q.size;x++)if(q.getModule(x,y))ctx.fillRect((x+border)*scale,(y+border)*scale,scale,scale);$('#qr').replaceChildren(canvas);}
$('#copy-code').onclick=async()=>{const code=$('#manual-code').textContent.replace(/[-\s]/g,'');try{if(navigator.clipboard&&window.isSecureContext)await navigator.clipboard.writeText(code);else{const box=document.createElement('textarea');box.value=code;document.body.append(box);box.select();const ok=document.execCommand('copy');box.remove();if(!ok)throw Error();}toast('已複製 Matter 配對碼');}catch{toast('請長按手動配對碼以複製');}};
async function loadNetworks(){const btn=$('#wifi-refresh');btn.disabled=true;try{const r=await fetch('/provision/config',{cache:'no-store',signal:AbortSignal.timeout(8000)});if(!r.ok)throw Error();const data=await r.json();if(data.local_url){const a=$('#local-address');a.href=data.local_url;a.textContent=data.local_url;a.hidden=false;}const list=$('#wifi-list');list.replaceChildren(new Option('選擇網路，或在下方手動輸入',''));const seen=new Set();for(const ap of (data.aps||[]).sort((a,b)=>b.rssi-a.rssi)){if(!ap.ssid||seen.has(ap.ssid))continue;seen.add(ap.ssid);list.add(new Option(ap.ssid+' · '+ap.rssi+' dBm',ap.ssid));}if(!seen.size)list.options[0].textContent='清單暫無網路，請在下方輸入名稱';}catch{$('#wifi-message').textContent='無法讀取網路清單；請確認裝置連線。';}finally{btn.disabled=false;}}
$('#wifi-refresh').onclick=loadNetworks;$('#wifi-list').onchange=e=>{if(e.target.value)$('#ssid').value=e.target.value;};
$('#show-password').onclick=()=>{const show=$('#psk').type==='password';$('#psk').type=show?'text':'password';$('#show-password').textContent=show?'隱藏':'顯示';};
$('#wifi-form').onsubmit=async e=>{e.preventDefault();const ssid=$('#ssid').value,psk=$('#psk').value;if(new TextEncoder().encode(ssid).length>32){$('#wifi-message').textContent='Wi-Fi 名稱長度超過限制。';return;}if(psk&&new TextEncoder().encode(psk).length<8){$('#wifi-message').textContent='Wi-Fi 密碼至少需要 8 個字元。';return;}$('#wifi-save').disabled=true;$('#wifi-message').textContent='正在儲存…';try{const r=await fetch('/provision/save',{method:'POST',headers:{'X-BC7215-Request':'1','Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({ssid,psk}),signal:AbortSignal.timeout(8000)});if(!r.ok)throw Error();$('#psk').value='';$('#wifi-message').textContent='設定已送達，正在嘗試連線（尚未確認成功）。\n若備用 AP 消失，請回到家中網路並開啟裝置的新 IP。';}catch{$('#wifi-message').textContent='未能確認儲存結果，請確認裝置連線後重試。';}finally{$('#wifi-save').disabled=false;}};
const events=new EventSource('/events');events.onopen=()=>setConnection(true);events.onerror=()=>setConnection(false);
events.addEventListener('state',event=>{try{const data=JSON.parse(event.data);if(!data.id)return;entities.set(data.id,{...entities.get(data.id),...data});if(domainOf(data)==='climate'&&Number(data.target_temperature)===pendingTemperature)pendingTemperature=null;render();}catch{toast('收到無法解析的裝置資料，等待下一次更新');}});
events.addEventListener('ping',()=>{if(!connected)setConnection(true);});
loadNetworks();render();
