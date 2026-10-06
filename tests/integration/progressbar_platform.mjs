import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {NativeRequests} from './native_requests.mjs';

// External filesystem/clipboard protocol only. No disk or OS clipboard access;
// actual application callbacks create files, write contents, close handles,
// start transfers, consume progress, unlink and schedule their own timers.
export function progressbarPlatform(trace, config, token) {
  const hosts = new Map(), files = new Map(), handles = [], listeners = [], timers = [], toasts = [];
  const rejection = trace.tag({code: config.code ?? 7, message: 'platform rejection'}, 'platform.rejection');
  const queue = new NativeRequests(trace, config, rejection,
    error => Object.is(error, rejection) ? 'platform rejection' : trace.outcome(error));
  let clipboard;
  const digest = value => createHash('sha256').update(value).digest('hex');
  const prompt = trace.surface('prompt', {showToast(value) {toasts.push(value); return token;}});
  const context = trace.surface('host.context', {distributedFilesDir: '/shared', filesDir: '/local'});
  const ui = trace.surface('ui.context', {getHostContext: () => context, getPromptAction: () => prompt});
  const uri = path => 'uri:' + path;
  const load = (name, version = 0) => {
    const key = name + ':' + version; if (hosts.has(key)) return hosts.get(key);
    const surface = members => trace.surface(name + ':' + version, members);
    let host;
    switch (name) {
      case '@ohos:file.fs':
        host = surface({OpenMode: trace.surface('open.mode' + version, {READ_WRITE: 1, CREATE: 2}),
          openSync(path, flags) {
            assert.equal(flags, 3); assert.equal(typeof path, 'string');
            const record = {path, fd: handles.length + 23, closed: false}; handles.push(record);
            if (!files.has(path)) files.set(path, '');
            return trace.surface('file' + record.fd, {fd: record.fd});
          },
          writeSync(fd, value) {
            const record = handles.find(record => record.fd === fd); assert.ok(record && !record.closed);
            assert.equal(typeof value, 'string'); files.set(record.path, value); return value.length;
          },
          closeSync(file) {
            const record = handles.find(record => record.fd === file.fd); assert.ok(record && !record.closed);
            record.closed = true; return token;
          },
          unlink(path) {files.delete(path); return queue.request('unlink', undefined);},
        }); break;
      case '@ohos:file.fileuri': host = surface({getUriFromPath: path => uri(path)}); break;
      case '@ohos:hilog': host = surface({info: () => token, error: () => token}); break;
      case '@ohos:pasteboard': {
        const board = trace.surface('board' + version, {
          setData(data) {clipboard = data; return queue.request('set', undefined);},
          getDataWithProgress(params) {
            assert.equal(typeof params.progressListener, 'function');
            listeners.push(params); trace.event('platform.progress.listener', params);
            return queue.request('get', clipboard).then(data => {
              files.set(params.destUri, 'transferred:' + (data?.uri ?? 'empty')); return data;
            });
          },
        });
        host = surface({MIMETYPE_TEXT_URI: 'text/uri',
          ProgressIndicator: trace.surface('indicator' + version, {NONE: 0}),
          createData(type, value) {return {type, uri: value};}, getSystemPasteboard: () => board}); break;
      }
      default: assert.fail('unmodeled progress import ' + name);
    }
    hosts.set(key, host); return host;
  };
  return {load, ui, queue,
    timeout(callback, delay) {timers.push({callback, delay}); trace.event('platform.timer', delay, callback); return timers.length;},
    progress(call) {
      const values = config.progress ?? [0, 37, 100];
      for (const params of config.reverse ? [...listeners].reverse() : listeners)
        for (const value of values) call('progress.callback', params.progressListener, {foreign: true}, [{progress: value}]);
    },
    timers(call) {for (const entry of timers) call('timer.callback', entry.callback, {foreign: true});},
    snapshot() {return {...queue.snapshot(), clipboard: trace.normalize(clipboard),
      files: [...files].map(([path, value]) => [path, value.length, digest(value)]), handles,
      listeners: listeners.map(({destUri, progressIndicator}) => [destUri, progressIndicator]),
      timers: timers.map(({delay}) => delay), toasts: toasts.map(value => trace.normalize(value))};},
  };
}
