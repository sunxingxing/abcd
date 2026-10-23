// A hoisted function captures a `let` declared after its creation and
// reassigned later; the declaring store dominates every other access.
export function arm(o, log) {
  return new Promise(async (resolve, reject) => {
    let timer = null;
    function done(v) { log.push('done:' + (timer === null ? 'none' : 'armed')); resolve(v); }
    if (!o.quick) {
      timer = 'armed';
    }
    try {
      await o.run();
      done('ok');
    } catch (e) {
      reject(e);
    }
  });
}
