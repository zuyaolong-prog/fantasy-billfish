(() => {
  'use strict';
  if (globalThis.__billfishMaintainedVideoContent) return;
  globalThis.__billfishMaintainedVideoContent = true;
  // Native <video> is not draggable by default. Set it only on the video the user touches.
  document.addEventListener('mousedown', event => {
    if (event.button === 0 && event.target && event.target.nodeName === 'VIDEO') event.target.draggable = true;
  }, true);
  function showNotice(message) {
    if (!message || message.billfishVideoCaptureResult !== true) return;
    const old = document.getElementById('billfish-maintained-video-notice');
    if (old) old.remove();
    const notice = document.createElement('div');
    notice.id = 'billfish-maintained-video-notice';
    notice.role = 'status';
    if (message.ok) notice.textContent = '已发送到 Billfish, 请在弹窗中选择视频分辨率.';
    else if (message.outcome_unknown) {
      notice.textContent = `视频采集结果尚未确认. ${message.job_id?`任务 ID: ${message.job_id}. `:''}请先检查 Billfish 中的采集任务, 确认结果后再尝试, 避免重复下载. ${message.error && message.error.message || ''}`;
    } else notice.textContent = `视频采集失败: ${message.error && message.error.message || '无法连接 Billfish.'}`;
    Object.assign(notice.style, {position:'fixed',right:'20px',bottom:'24px',zIndex:'2147483647',maxWidth:'420px',padding:'14px 18px',borderRadius:'9px',background:message.ok?'#24523b':'#7e2929',color:'#fff',font:'14px/1.5 system-ui, sans-serif',boxShadow:'0 4px 24px #0005',pointerEvents:'none'});
    (document.body || document.documentElement).appendChild(notice);
    if (!message.outcome_unknown) setTimeout(() => notice.remove(), message.ok ? 5000 : 12000);
  }
  chrome.runtime.onMessage.addListener(showNotice);
  globalThis.BillfishCaptureVideoNotice = showNotice;
  globalThis.BillfishCaptureVideoUi = Object.freeze({decorate(frame) {
    if (!frame) return;
    frame.dataset.captureType = 'video';
    const panel = frame.querySelector('#show-scoll-mouse-top');
    if (panel) panel.style.width = '240px';
    for (const folders of frame.querySelectorAll('.file-list')) folders.style.display = 'none';
    for (const node of frame.querySelectorAll('.mouse-show-first')) node.textContent = node.closest('#origin-shows') ? '拖放视频到这里' : '松开后选择视频分辨率';
    [...frame.querySelectorAll('.mouse-show-next')].forEach((node,index) => { node.textContent = index % 2 ? '浏览器采集文件夹' : '下载后保存至'; });
    [...frame.querySelectorAll('.p-up-here')].forEach((node,index) => { node.textContent = index % 2 ? '下载后保存至浏览器采集文件夹' : index < 2 ? '拖放视频到这里' : '松开后选择视频分辨率'; });
  }});
  if (typeof window === 'undefined' || window.top !== window.self) return;
  const api = globalThis.BillfishCaptureVideo;
  function request(message) {
    return new Promise((resolve,reject) => chrome.runtime.sendMessage(message,response => {
      if (chrome.runtime.lastError) reject(new Error(chrome.runtime.lastError.message));
      else resolve(response);
    }));
  }
  let entry = null, select = null, button = null, shownSignature = '';
  const controller = api.createPageCapture({
    discover:() => api.discoverPage(document,location.href,document.title),
    readPolicy:async() => {
      const response = await request({billfishCaptureAction:'getVideoPolicy'});
      return response && response.ok && response.policy;
    },
    submit:async(candidate) => {
      const response = await request({billfishCaptureAction:'captureDetectedVideo',candidate:{src:candidate.src,pageUrl:candidate.pageUrl,title:candidate.title,kind:candidate.kind}});
      // Native capture already notifies this tab. Policy/context rejection did not reach the native helper.
      if (response && response.billfishVideoCaptureResult && !response.page_notice_sent) showNotice(response);
      return response;
    },
    notify:showNotice,
    render:state => {
      if (!state.candidates.length) {
        if (entry) entry.remove();
        entry = select = button = null;
        shownSignature = '';
        return;
      }
      if (!entry) {
        entry = document.createElement('div');
        entry.id = 'billfish-video-capture-entry';
        Object.assign(entry.style,{position:'fixed',right:'20px',top:'100px',zIndex:'2147483646'});
        const shadow = entry.attachShadow({mode:'open'});
        shadow.innerHTML = '<style>:host{all:initial}section{display:flex;gap:8px;padding:8px;border:1px solid #ffffff35;border-radius:12px;background:#202733;box-shadow:0 4px 20px #0004;font:14px/1.4 system-ui,sans-serif}button,select{font:inherit;border:0;border-radius:7px;padding:9px 12px}button{background:#4a79ff;color:white;cursor:pointer;white-space:nowrap}button:disabled{opacity:.6;cursor:wait}select{max-width:220px;background:#fff;color:#202733}select[hidden]{display:none}</style><section aria-label="Billfish 视频采集"><select aria-label="选择网页视频"></select><button type="button">采集视频</button></section>';
        select = shadow.querySelector('select');
        button = shadow.querySelector('button');
        button.addEventListener('click',() => controller.capture(select.value));
        (document.body || document.documentElement).appendChild(entry);
      }
      const nextSignature = JSON.stringify(state.candidates.map(candidate => [api.candidateKey(candidate),candidate.title]));
      if (nextSignature !== shownSignature) {
        const previous = select.value;
        select.replaceChildren();
        state.candidates.forEach((candidate,index) => {
          const option = document.createElement('option');
          option.value = api.candidateKey(candidate);
          option.textContent = state.candidates.length>1?`${index+1}. ${candidate.title}`:candidate.title;
          select.appendChild(option);
        });
        if ([...select.options].some(option => option.value === previous)) select.value = previous;
        shownSignature = nextSignature;
      }
      select.hidden = state.candidates.length < 2;
      select.disabled = button.disabled = state.busy;
      button.textContent = state.busy ? '正在发送...' : '采集视频';
      button.title = '选择分辨率后下载至 Billfish 浏览器采集文件夹';
    }
  });
  let scheduled = false;
  function schedule(force = false) {
    if (force) { void controller.refresh(true); return; }
    if (scheduled) return;
    scheduled = true;
    setTimeout(() => { scheduled = false; void controller.refresh(); },100);
  }
  new MutationObserver(() => schedule()).observe(document.documentElement,{childList:true,subtree:true,attributes:true,attributeFilter:['src']});
  document.addEventListener('loadedmetadata',() => schedule(),true);
  document.addEventListener('emptied',() => schedule(),true);
  document.addEventListener('yt-navigate-finish',() => schedule());
  window.addEventListener('popstate',() => schedule());
  window.addEventListener('hashchange',() => schedule());
  window.addEventListener('focus',() => schedule(true));
  let lastUrl = location.href;
  setInterval(() => { if (lastUrl !== location.href) { lastUrl = location.href; schedule(); } },1000);
  schedule();
})();
