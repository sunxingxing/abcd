// Loop variables captured by closures: es2abc gives each iteration a fresh
// environment and stores the iteration value into it.
export function ofValues(xs, keep) {
  for (const x of xs) {
    keep(() => x);
  }
}
export function inKeys(o, keep) {
  for (const k in o) {
    keep(() => k + '!');
  }
}
