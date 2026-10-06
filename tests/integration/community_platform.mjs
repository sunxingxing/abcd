import assert from 'node:assert/strict';

const abilityName = 'phone/src/main/ets/entryability/EntryAbility';
const loggerName = 'base/src/main/ets/utils/Logger';

// Controlled platform only; real Ability/Logger/Breakpoint bodies and shared
// AppStorage remain in the complete generated/reference module graph. Retained
// platform callbacks reuse the same queue and foreign receiver as UI callbacks.
export function communityPlatform(trace, config, token, enqueue) {
  const abilities = [], listeners = [], methods = [], loggerCalls = [];
  const values = new Map();
  const scalar = (value, name) => {
    if (!config.coerce) return value;
    let object;
    object = {[Symbol.toPrimitive](hint) {
      assert.equal(this, object); trace.event('platform.coerce', name, hint); return value;
    }};
    return trace.tag(object, name);
  };
  const displayRecord = {};
  Object.defineProperty(displayRecord, 'densityPixels', {get() {
    trace.event('display.density'); return scalar(Object.hasOwn(config, 'density') ? config.density : 1, 'density');
  }});
  const load = (name, version = 0) => {
    const key = name + ':' + version;
    if (values.has(key)) return values.get(key);
    let value;
    if (name === '@ohos:app.ability.UIAbility') {
      value = class UIAbility {
        constructor(...args) {trace.tag(this, 'ability' + abilities.length); abilities.push(this); trace.event('ability.super', new.target.name, ...args);}
      };
    } else if (name === '@ohos:deviceInfo') {
      value = trace.surface('device' + (version || ''), {deviceType: config.deviceType ?? 'phone'});
    } else {
      value = trace.surface(name + (version ? ':' + version : ''), {
        info() {return undefined;}, debug() {return undefined;}, error() {return undefined;}, warn() {return undefined;},
        exit() {return undefined;}, setImageRawDataCacheSize() {return undefined;}, setImageCacheCount() {return undefined;},
        getDefaultDisplaySync() {return config.ability ? displayRecord : {width: 960, height: 720};},
      });
    }
    values.set(key, value); return value;
  };
  const window = number => {
    const rectangle = {};
    Object.defineProperty(rectangle, 'width', {get() {
      trace.event('window.width', number); return scalar(Object.hasOwn(config, 'width') ? config.width : 700, 'width');
    }});
    return trace.surface('window' + number, {
      setWindowLayoutFullScreen() {return token;},
      getWindowProperties() {return {windowRect: rectangle};},
      on(name, callback) {
        assert.equal(name, 'windowSizeChange'); assert.equal(typeof callback, 'function');
        listeners.push({number, callback}); trace.event('window.listener', number, callback); return token;
      },
    });
  };
  const error = (number, kind, code) => {
    const value = {};
    Object.defineProperty(value, 'code', {enumerable: true, get() {
      trace.event('stage.error.code', number, kind); return code;
    }});
    return trace.tag(value, 'stage.error' + number + ':' + kind);
  };
  const stage = number => {
    const win = window(number);
    return trace.surface('stage' + number, {
      getMainWindow(callback) {enqueue('ability.window', callback, [error(number, 'window', config.mainCode ?? 0), win]); return token;},
      loadContent(path, callback) {
        assert.equal(path, 'pages/MainPage');
        const data = {toJSON() {trace.event('stage.data.json', number); return {loaded: true};}};
        enqueue('ability.content', callback, [error(number, 'content', config.contentCode ?? 0), data]); return token;
      },
    });
  };
  const run = (modules, drain) => {
    if (!config.ability) return;
    const module = modules.get(abilityName), loggerModule = modules.get(loggerName);
    if (module.status !== 'evaluated' || loggerModule.status !== 'evaluated') return;
    const Type = module.namespace.default, logger = loggerModule.namespace.default;
    const parameter = trace.tag({}, 'ability.parameter');
    for (let number = 0; number < (config.concurrent ? 2 : 1); ++number) {
      const owner = trace.call('ability.construct', () => new Type(parameter), undefined);
      if (!owner) continue;
      // Expose captured instance identity without replacing any method body:
      // the caller-owned accessor returns the actual prototype method. Reads
      // must still occur on the right instance at the real callback time.
      const original = owner.updateBreakpoint;
      Object.defineProperty(owner, 'updateBreakpoint', {configurable: true, get() {
        assert.equal(this, owner); trace.event('ability.breakpoint.get', number); return original;
      }});
      for (const name of ['onCreate', 'onDestroy', 'onForeground', 'onBackground', 'onWindowStageDestroy']) {
        methods.push(name); trace.call('ability.' + name, owner[name], owner, [parameter, parameter]);
      }
      methods.push('onWindowStageCreate');
      trace.call('ability.onWindowStageCreate', owner.onWindowStageCreate, owner, [stage(number)]);
    }
    drain();
    const shared = trace.tag({}, 'logger.argument');
    const args = Array.from({length: config.loggerArgumentCount ?? 3}, (_, i) => i % 2 ? shared : i);
    for (const key of ['debug', 'info', 'warn', 'error']) {
      loggerCalls.push(key); trace.call('logger.' + key, logger[key], logger, args);
    }
  };
  const resize = drain => {
    if (!config.ability) return;
    for (const width of config.resizeWidths ?? [599, 600, 840]) {
      for (const listener of config.reverse ? [...listeners].reverse() : listeners) {
        const size = {};
        Object.defineProperty(size, 'width', {get() {trace.event('resize.width', listener.number); return scalar(width, 'resize.width');}});
        trace.call('ability.resize', listener.callback, {foreign: true}, [size]);
      }
      drain();
    }
  };
  const snapshot = modules => ({methods, loggerCalls, listeners: listeners.length,
    abilities: abilities.map(owner => trace.normalize(Reflect.ownKeys(owner).map(key => [key, Object.getOwnPropertyDescriptor(owner, key)]))),
    logger: config.ability && modules.get(loggerName).status === 'evaluated'
      ? trace.normalize(Reflect.ownKeys(modules.get(loggerName).namespace.default).map(key =>
        [key, Object.getOwnPropertyDescriptor(modules.get(loggerName).namespace.default, key)])) : undefined,
  });
  const swap = imports => {
    if (!config.ability) return;
    for (const [name, module] of imports) if (name !== '@ohos:app.ability.UIAbility') module.setExport('default', load(name, 1));
  };
  return {load, run, resize, snapshot, swap};
}
