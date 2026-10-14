export function ordinary(xs) {
  for (const x of xs) {
    if (x === 1) break;
    if (x === 2) return x;
  }
  return 0;
}
export function throwing(xs) {
  for (const x of xs) {
    throw x;
  }
}
export function nested(xs, ys) {
  for (const x of xs) {
    for (const y of ys) {
      if (x === y) return x;
    }
  }
}
