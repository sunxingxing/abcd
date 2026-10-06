import assert from 'node:assert/strict';
import {NativeRequests} from './native_requests.mjs';

// SDK boundaries only: native styled objects and controllers retain independent
// identities and contents. Clipboard records preserve entry order, asynchronous
// data requests, mutable type/record reads and exact pixel references.
export function richTextPlatform(trace, config, token) {
  const buffers = [], hosts = new Map(), styles = [], attachments = [], controllers = [], paths = [], records = [], data = [], toasts = [];
  const styleState = new WeakMap(), attachmentState = new WeakMap(), controllerState = new WeakMap(), recordState = new WeakMap(), dataState = new WeakMap();
  const rejection = trace.tag({code: 7, message: 'platform rejection'}, 'platform.rejection');
  const queue = new NativeRequests(trace, config, rejection,
    error => Object.is(error, rejection) ? 'platform rejection' : trace.outcome(error));
  let navigate = () => {}, clipboard;
  const pixel = trace.tag({then: undefined, width: 57, height: 57}, 'pixel');
  const invoke = (owner, name, body) => {
    Object.defineProperty(owner, name, {configurable: true, get() {
      trace.event('platform.method.get', name, owner);
      const method = function(...args) {assert.equal(this, owner); trace.event('platform.method.call', name, owner, ...args); return Reflect.apply(body, owner, args);};
      for (const adapter of ['call', 'apply', 'bind']) Object.defineProperty(method, adapter, {get() {assert.fail('ambient SDK adapter');}});
      return method;
    }});
  };
  class StyledString {
    constructor(input) {
      const chunks = attachmentState.has(input) ? [{image: input}] : [{text: input}];
      styleState.set(this, chunks); trace.tag(this, 'style' + styles.length); styles.push(this);
      trace.event('platform.styled.new', input);
      invoke(this, 'getString', () => styleState.get(this).map(chunk => chunk.text ?? '').join(''));
    }
  }
  class MutableStyledString extends StyledString {
    constructor(input) {
      super(input);
      invoke(this, 'appendStyledString', other => {styleState.get(this).push(...styleState.get(other)); return token;});
    }
  }
  class ImageAttachment {
    constructor(options) {
      attachmentState.set(this, options); trace.tag(this, 'attachment' + attachments.length); attachments.push(this);
      trace.event('platform.attachment.new', options);
    }
  }
  invoke(StyledString, 'fromHtml', html => queue.request('html', new StyledString('html:' + html)));
  invoke(StyledString, 'toHtml', styled => '<p>' + styleState.get(styled).map(chunk => chunk.text ?? '[image]').join('') + '</p>');
  class RichEditorStyledStringController {
    constructor() {
      const state = {styled: new StyledString('')}; controllerState.set(this, state);
      trace.tag(this, 'styled.controller' + controllers.length); controllers.push(this); trace.event('platform.controller.new', 'styled');
      invoke(this, 'setStyledString', value => {assert.ok(styleState.has(value)); state.styled = value; return token;});
      invoke(this, 'getStyledString', () => state.styled);
    }
  }
  class RichEditorController {
    constructor() {
      const state = {spans: []}; controllerState.set(this, state);
      trace.tag(this, 'pixel.controller' + controllers.length); controllers.push(this); trace.event('platform.controller.new', 'pixel');
      invoke(this, 'deleteSpans', () => {state.spans = []; return token;});
      invoke(this, 'addImageSpan', (image, options) => {state.spans.push([image, options]); return state.spans.length;});
      invoke(this, 'fromStyledString', styled => {
        const chunks = styleState.get(styled); assert.ok(chunks);
        if (Object.hasOwn(config, 'spans')) return config.spans === null ? null
          : config.spans.map(value => value === 'pixel' ? {valuePixelMap: pixel} : value === 'null' ? null
            : value === 'empty' ? {} : {valuePixelMap: value});
        return chunks.filter(chunk => chunk.image).map(chunk => ({valuePixelMap: attachmentState.get(chunk.image).value}));
      });
    }
  }
  class NavPathStack {
    constructor() {
      trace.tag(this, 'path' + paths.length); const state = {object: this, pushes: []}; paths.push(state);
      trace.event('platform.path.new');
      invoke(this, 'pushPathByName', (name, params, flag) => {state.pushes.push([name, params, flag]); navigate(name); return token;});
    }
  }
  const record = entries => {
    const object = trace.surface('record' + records.length, {
      getValidTypes(targets) {
        const keys = [...entries.keys()].filter(key => targets.includes(key));
        const selected = config.extraType ? [...keys, 'other'] : keys;
        return new Proxy(selected, {get(target, key, receiver) {
          trace.event('platform.types.get', String(key)); return Reflect.get(target, key, receiver);
        }});
      },
      getData(type) {return queue.request('data', entries.get(type));},
      addEntry(type, value) {entries.set(type, value); return token;},
    }); recordState.set(object, entries); records.push(object); return object;
  };
  const pasteData = rows => {
    const object = trace.surface('data' + data.length, {then: undefined,
      getRecordCount() {
        if (config.shrink && rows.length > 1) rows.pop(); return rows.length;
      }, getRecord: index => rows[index]});
    dataState.set(object, rows); data.push(object); return object;
  };
  const initial = config.records ?? [['html', 'text', 'pixel'], ['text']];
  clipboard = pasteData(initial.map(types => record(new Map(types.map(type => [type,
    type === 'html' ? (Object.hasOwn(config, 'html') ? config.html : '<b>source</b>') : type === 'pixel' ? pixel : type === 'uri' ? 'PageABuilder' : 'text:' + type])))));
  const prompt = trace.surface('prompt', {showToast(value) {toasts.push(value); return token;}});
  const resource = trace.surface('resource', {getMediaContent(id) {
    assert.equal(id, 16777251); const array = trace.tag(new Uint8Array([3, 7]), 'media.array' + buffers.length);
    // Equal bytes do not prove the buffer copy's identity. Preserve the input
    // identity and record each fresh image-source buffer independently.
    trace.tag(array.buffer, 'media.buffer' + buffers.length); buffers.push(array.buffer);
    return queue.request('media', array);
  }});
  const context = trace.surface('host.context', {resourceManager: resource});
  const ui = trace.surface('ui.context', {getHostContext: () => context, getPromptAction: () => prompt});
  const load = (name, version = 0) => {
    const key = name + ':' + version; if (hosts.has(key)) return hosts.get(key);
    const surface = members => trace.surface(name + ':' + version, members);
    let host;
    switch (name) {
      case '@ohos:hilog': host = surface({info: () => token, error: () => token}); break;
      case '@ohos:multimedia.image': host = surface({createImageSource(buffer) {
        if (!trace.tags.has(buffer)) {trace.tag(buffer, 'image.buffer' + buffers.length); buffers.push(buffer);}
        const contents = [...new Uint8Array(buffer)]; trace.event('platform.image.bytes', contents, buffer);
        return trace.surface('image.source' + version, {createPixelMap: () => queue.request('pixel', pixel)});
      }}); break;
      case '@ohos:pasteboard': {
        const board = trace.surface('board' + version, {
          getData: () => queue.request('get', clipboard),
          setData(value) {assert.ok(dataState.has(value)); clipboard = value; return queue.request('set', undefined);},
        });
        host = surface({MIMETYPE_TEXT_HTML: 'html', MIMETYPE_TEXT_PLAIN: 'text', MIMETYPE_PIXELMAP: 'pixel', MIMETYPE_TEXT_URI: 'uri',
          createData(type, value) {return pasteData([record(new Map([[type, value]]))]);}, getSystemPasteboard: () => board}); break;
      }
      default: assert.fail('unmodeled rich import ' + name);
    }
    hosts.set(key, host); return host;
  };
  return {load, ui, queue, pixel,
    globals: {StyledString, MutableStyledString, ImageAttachment, RichEditorController, RichEditorStyledStringController, NavPathStack},
    navigator(fn) {navigate = fn;},
    snapshot() {
      return {...queue.snapshot(), buffers: buffers.map(value => [trace.normalize(value), [...new Uint8Array(value)]]), clipboard: trace.normalize(clipboard),
        styles: styles.map(value => [trace.normalize(value), styleState.get(value).map(chunk => trace.normalize(chunk))]),
        attachments: attachments.map(value => [trace.normalize(value), trace.normalize(attachmentState.get(value))]),
        controllers: controllers.map(value => [trace.normalize(value), trace.normalize(controllerState.get(value))]),
        records: records.map(value => [trace.normalize(value), [...recordState.get(value)].map(([type, payload]) => [type, trace.normalize(payload)])]),
        data: data.map(value => [trace.normalize(value), dataState.get(value).map(row => trace.normalize(row))]),
        paths: paths.map(({object, pushes}) => [trace.normalize(object), pushes]), toasts: toasts.map(value => trace.normalize(value))};
    },
  };
}
