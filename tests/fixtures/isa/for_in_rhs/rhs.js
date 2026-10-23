// for-in whose right-hand side is a property read or call: es2abc protects
// it, with the acquisition, by one transparent rethrow region.
export function specs(o, s) {
  if (s.shuffle()) {
    s.log('shuffled');
  }
  for (let k in o.specs) {
    if (s.stop(k)) {
      break;
    }
    s.log(k);
  }
  return 'done';
}
export function computed(o, s) {
  for (const k in o[s.key()]) {
    s.log(k);
  }
}
