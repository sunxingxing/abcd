import { current, bump } from './dependency.js';
import { extra } from './extra.js';
function explicit(value) {
  "use concurrent";
  return value + 1;
}
function implicit(log) {
  "use concurrent";
  log.push('implicit');
}
function failed(log) {
  "use concurrent";
  log.push('throw');
  throw new RangeError('failure');
}
function handled(value, log) {
  "use concurrent";
  try {
    log.push('try');
    if (value) throw new TypeError('body');
    return 7;
  } catch (error) {
    log.push(error.constructor.name);
    return 9;
  } finally {
    log.push('finally');
  }
}
function imported(value) {
  "use concurrent";
  bump(value);
  return current + extra;
}
function forwarded(value) {
  "use concurrent";
  function child() { return current; }
  return child() + value;
}

export {explicit, implicit, failed, handled, imported, forwarded};
