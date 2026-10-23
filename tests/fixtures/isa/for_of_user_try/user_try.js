// User try statements nested inside for-of bodies: catch with effects,
// continue/rethrow/replacement throw from the catch, async awaits.
export function logged(xs, o) {
  for (const x of xs) {
    try { o.push(x); } catch (e) { o.log('caught:' + e.message); }
  }
}
export function skipping(xs, o) {
  for (const x of xs) {
    try { o.push(x); } catch (e) { o.log('skip:' + e.message); continue; }
    o.after(x);
  }
}
export function replacing(xs, o) {
  for (const x of xs) {
    try { o.push(x); } catch (e) { throw new RangeError('wrapped:' + e.message); }
  }
}
export async function rethrowing(xs) {
  for (const hook of xs) {
    try { await hook(); } catch (e) { e.message = e.message + ' in hook'; throw e; }
  }
}
export async function tolerant(xs, o) {
  for (const hook of xs) {
    try { await hook(); } catch (e) { o.log('async:' + e.message); }
  }
}
