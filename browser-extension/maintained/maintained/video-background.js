importScripts('maintained/video-common.js');
(() => {
  'use strict';
  const HOST = 'com.billfish.capture';
  let policyReader = null;
  async function readPolicy(host) {
    if (!policyReader) return globalThis.BillfishCaptureVideo.pagePolicy(null);
    try {
      return globalThis.BillfishCaptureVideo.pagePolicy(await policyReader(host));
    } catch { return globalThis.BillfishCaptureVideo.pagePolicy(null); }
  }
  function sendNative(message) {
    return new Promise((resolve, reject) => {
      chrome.runtime.sendNativeMessage(HOST, message, response => {
        const error = chrome.runtime.lastError;
        if (error) reject(new Error(error.message));
        else resolve(response);
      });
    });
  }
  async function capture(item) {
    let result;
    try {
      const response = await sendNative(globalThis.BillfishCaptureVideo.serializeCapture(item));
      if (response && response.ok && response.accepted) {
        result = {billfishVideoCaptureResult:true,ok:true,job_id:response.job_id};
      } else {
        const error = response && response.error || {};
        result = {billfishVideoCaptureResult:true,ok:false,error:{
          code:String(error.code || 'capture_rejected').slice(0, 100),
          message:String(error.message || 'Billfish 未接受视频采集请求.').slice(0, 1000)
        }};
        // A timeout can happen after submission. Preserve its identity; never resubmit here.
        if (response && typeof response.job_id === 'string' && response.job_id) {
          result.job_id = response.job_id;
          result.outcome_unknown = true;
        }
      }
    } catch (error) {
      result = {billfishVideoCaptureResult:true,ok:false,error:{code:'native_host_error',message:String(error.message || error).slice(0, 1000)}};
    }
    if (Number.isInteger(item.tabId) && item.tabId >= 0) {
      // A page may close while the native dialog opens. That does not invalidate the job.
      try { await chrome.tabs.sendMessage(item.tabId, result); result.page_notice_sent = true; } catch (_) { }
    }
    return result;
  }
  if (chrome.runtime.onMessage) chrome.runtime.onMessage.addListener((message,sender,reply) => {
    if (!message || !['getVideoPolicy','captureDetectedVideo'].includes(message.billfishCaptureAction)) return;
    (async () => {
      const page = sender && sender.tab && globalThis.BillfishCaptureVideo.httpUrl(sender.tab.url);
      if (!page || sender.frameId !== 0) return {billfishVideoCaptureResult:true,ok:false,error:{code:'page_context_unavailable',message:'当前页面无法采集视频.'}};
      const policy = await readPolicy(page.host);
      if (message.billfishCaptureAction === 'getVideoPolicy') return {ok:true,policy};
      if (!policy.allowed) return {billfishVideoCaptureResult:true,ok:false,error:{code:policy.code,message:policy.message}};
      const current = await chrome.tabs.get(sender.tab.id);
      if (!current || current.url !== page.href) return {billfishVideoCaptureResult:true,ok:false,error:{code:'video_candidate_changed',message:'页面视频已变化, 请重新选择当前视频.'}};
      const item = message.candidate;
      if (!item || item.pageUrl !== page.href || !globalThis.BillfishCaptureVideo.httpUrl(item.src)) return {billfishVideoCaptureResult:true,ok:false,error:{code:'video_candidate_changed',message:'页面视频已变化, 请重新选择当前视频.'}};
      const pageKind = globalThis.BillfishCaptureVideo.classifyUrl(page.href);
      if (pageKind && item.src !== page.href) return {billfishVideoCaptureResult:true,ok:false,error:{code:'video_candidate_changed',message:'页面视频已变化, 请重新选择当前视频.'}};
      return capture({src:item.src,pageUrl:page.href,title:item.title,tabId:sender.tab.id});
    })().then(reply,error => reply({billfishVideoCaptureResult:true,ok:false,error:{code:'capture_failed',message:String(error.message || error).slice(0,1000)}}));
    return true;
  });
  globalThis.BillfishCaptureNative = Object.freeze({capture,setPolicyReader(reader) { policyReader = reader; }});
})();
