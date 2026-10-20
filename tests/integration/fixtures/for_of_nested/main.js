export function scan(xs, ys, visit, stop) {
  for (const x of xs) {
    for (const y of ys) {
      visit(x, y);
      if (stop) return y;
    }
  }
}
