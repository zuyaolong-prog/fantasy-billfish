(function (root, factory) {
  const api = factory();
  if (typeof module === 'object' && module.exports) module.exports = api;
  else root.BillfishCaptureVideo = api;
})(globalThis, function () {
  'use strict';
  const MEDIA_PATH = /\.(?:mp4|webm|mov|m4v|mkv|avi|ogv|mpeg|mpg|m3u8|mpd)$/i;
  function httpUrl(value, base) {
    if (typeof value !== 'string' || !value.trim()) return null;
    try {
      const url = new URL(value, base);
      return ['http:', 'https:'].includes(url.protocol) ? url : null;
    } catch { return null; }
  }
  function classifyUrl(value, base) {
    const url = httpUrl(value, base);
    if (!url) return null;
    const host = url.hostname.toLowerCase();
    if ((host === 'youtube.com' || host.endsWith('.youtube.com')) &&
        ((url.pathname === '/watch' && url.searchParams.get('v')) || /^\/(?:shorts|embed|live)\/[^/]+/.test(url.pathname))) return 'youtube';
    if (host === 'youtu.be' && /^\/[^/]+/.test(url.pathname)) return 'youtube';
    if ((host === 'bilibili.com' || host.endsWith('.bilibili.com')) && /^\/video\/(?:BV[a-zA-Z0-9]+|av\d+)(?:\/|$)/i.test(url.pathname)) return 'bilibili';
    if (host === 'b23.tv' && /^\/[^/]+/.test(url.pathname)) return 'bilibili';
    return MEDIA_PATH.test(url.pathname) ? 'direct' : null;
  }
  function fromElement(element, pageUrl, pageTitle) {
    if (!element || typeof element.closest !== 'function') return null;
    const anchor = element.closest('a[href]');
    const link = anchor && httpUrl(anchor.href, pageUrl);
    const linkKind = link && classifyUrl(link.href);
    const title = String(element.alt || (anchor && anchor.title) || pageTitle || '').slice(0, 1000);
    if (linkKind) return {src: link.href, type: 'video', title, kind: linkKind};
    const video = element.closest('video');
    if (!video) return null;
    const source = video.querySelector && video.querySelector('source[src]');
    const media = httpUrl(video.currentSrc || video.src || (source && source.src), pageUrl);
    const page = httpUrl(pageUrl);
    const pageKind = page && classifyUrl(page.href);
    // Platform blob/DASH players need their watch page for a complete format inventory.
    const capture = pageKind && pageKind !== 'direct' ? page : (media || page);
    return capture ? {src: capture.href, type: 'video', title, kind: pageKind || 'video-element'} : null;
  }
  function serializeCapture(item) {
    const url = httpUrl(item.src || item.url);
    if (!url) throw new Error('视频地址必须使用 HTTP 或 HTTPS.');
    const message = {action: 'captureVideo', url: url.href};
    const page = httpUrl(item.pageUrl || item.page_url);
    if (page) message.page_url = page.href;
    if (item.title) message.title = String(item.title).slice(0, 1000);
    if (page && url.href !== page.href) message.media_url = url.href;
    return message;
  }
  function checkPolicy(request) {
    return Promise.resolve(request).then(reply => {
      if (!reply || reply.error) return {allowed:false,message:'无法确认当前网站的采集设置, 请稍后重试.'};
      return {allowed:!reply.output,message:reply.output?'当前网站已禁用采集.':''};
    }, () => ({allowed:false,message:'无法确认当前网站的采集设置, 请稍后重试.'}));
  }
  function discoverPage(document, pageUrl, pageTitle) {
    const page = httpUrl(pageUrl);
    if (!page) return [];
    const kind = classifyUrl(page.href);
    if (kind) return [{src:page.href,pageUrl:page.href,title:String(pageTitle || '当前视频').slice(0,1000),kind,mediaSignature:''}];
    const found = new Map();
    for (const video of document.querySelectorAll('video')) {
      const source = video.querySelector('source[src]');
      // Declared sources change before currentSrc during a media transition; never retain the previous URL.
      const raw = video.src || source && source.src || video.currentSrc || '';
      if (!raw) continue;
      const media = httpUrl(raw,page.href);
      // A real blob-backed video can be handed to the page extractor, never as a blob download URL.
      if (!media && !String(raw).startsWith('blob:')) continue;
      const src = media ? media.href : page.href;
      if (!found.has(src)) found.set(src,{src,pageUrl:page.href,title:String(video.title || pageTitle || '网页视频').slice(0,1000),kind:'video-element',mediaSignature:JSON.stringify([video.src || '',source && source.src || '',video.currentSrc || ''])});
    }
    return [...found.values()];
  }
  function candidateKey(candidate) {
    return JSON.stringify([candidate.pageUrl,candidate.src,candidate.kind,candidate.mediaSignature || '']);
  }
  function pagePolicy(policy) {
    const unavailable = {allowed:false,code:'site_policy_unavailable',message:'无法确认当前网站的采集设置, 请稍后重试.'};
    if (!policy || typeof policy.allowed !== 'boolean' || !['allowed','user_site_disabled','vendor_site_disabled','site_policy_unavailable'].includes(policy.code) || policy.allowed !== (policy.code === 'allowed')) return unavailable;
    return {allowed:policy.allowed,code:policy.code,message:String(policy.message || (policy.allowed?'':unavailable.message)).slice(0,1000)};
  }
  function createPageCapture({discover,readPolicy,submit,render,notify}) {
    let candidates = [], signature = null, revision = 0, allowed = false, busy = false;
    const display = () => render({candidates:allowed?candidates:[],busy});
    async function permission() {
      try {
        return pagePolicy(await readPolicy());
      } catch { return pagePolicy(null); }
    }
    async function refresh(force = false) {
      const fresh = discover();
      const nextSignature = JSON.stringify(fresh.map(candidate => [candidateKey(candidate),candidate.title]));
      if (!force && nextSignature === signature) return;
      signature = nextSignature;
      candidates = fresh;
      const current = ++revision;
      allowed = false;
      display();
      if (!fresh.length) return;
      const policy = await permission();
      if (current !== revision) return;
      allowed = policy.allowed;
      display();
    }
    async function capture(key) {
      if (busy) return;
      let candidate = discover().find(item => candidateKey(item) === key);
      if (!candidate) { await refresh(true); return; }
      busy = true;
      display();
      try {
        const policy = await permission();
        if (!policy.allowed) {
          allowed = false;
          notify({billfishVideoCaptureResult:true,ok:false,error:{code:policy.code || 'site_capture_disabled',message:policy.message}});
          return;
        }
        // Re-read the live page after the asynchronous policy query. A click cannot submit an old SPA/media candidate.
        candidate = discover().find(item => candidateKey(item) === key);
        if (!candidate) { await refresh(true); return; }
        const response = await submit({...candidate});
        if (!response || response.billfishVideoCaptureResult !== true) {
          notify({billfishVideoCaptureResult:true,ok:false,outcome_unknown:true,error:{code:'capture_unconfirmed',message:'未收到采集回执, 请先检查 Billfish 中的任务, 确认后再尝试.'},...(response && response.job_id?{job_id:response.job_id}:{})});
        }
      } catch (error) {
        notify({billfishVideoCaptureResult:true,ok:false,outcome_unknown:true,error:{code:'capture_unconfirmed',message:String(error.message || error).slice(0,1000)}});
      } finally {
        busy = false;
        await refresh();
        display();
      }
    }
    return Object.freeze({refresh,capture});
  }
  async function finalizeDrag(record, inside, context, cleanup, send, notify) {
    if (record.finalized) return;
    record.finalized = true;
    // Everything needed after the policy reply belongs to this completed gesture.
    const policy = record.policy;
    const payload = {type:'video',src:record.candidate.src,title:record.candidate.title,collectReffer:context.collectReffer};
    cleanup();
    if (!inside) return;
    const permission = await policy;
    if (!permission.allowed) {
      notify({billfishVideoCaptureResult:true,ok:false,error:{code:'site_capture_disabled',message:permission.message}});
      return;
    }
    return send(payload);
  }
  return Object.freeze({httpUrl, classifyUrl, fromElement, serializeCapture, checkPolicy, finalizeDrag, discoverPage, candidateKey, pagePolicy, createPageCapture});
});
