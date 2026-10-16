export function iterate(xs, stop) {
  let key = "before";
  for (key of xs) {
    if (stop) break;
  }
  return key;
}
