export function outerContinue(xs, ys, choose) {
  outer: for (const x of xs) {
    for (const y of ys(x)) {
      if (choose(x, y)) continue outer;
    }
  }
}
