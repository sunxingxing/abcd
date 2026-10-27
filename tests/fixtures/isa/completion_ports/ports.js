export function first(xs, ys, visit) {
  for (const x of xs)
    for (const y of ys(x)) {
      if (visit(x, y)) return y;
    }
}

export function transfers(xs, ys, choose) {
  outer: for (const x of xs) {
    inner: for (const y of ys(x)) {
      const action = choose(x, y);
      if (action === 0) break inner;
      if (action === 1) continue;
      if (action === 2) break outer;
      if (action === 3) continue;
    }
    const action = choose(x);
    if (action === 6) break outer;
    if (action === 7) continue;
  }
  choose();
}

export function pattern(rows, visit) {
  for (const row of rows) {
    const [a, b] = row;
    visit(a, b);
  }
}

export async function suspended(xs, ys, visit) {
  for (const x of xs)
    for (const y of ys(x)) {
      if (await visit(x, y)) return y;
    }
}


export function throwNested(xs, ys, visit) {
  for (const x of xs) for (const y of ys(x)) {
    if (visit(x, y)) throw y;
  }
}

export function outerReturn(xs, ys, visit) {
  for (const x of xs) {
    for (const y of ys(x)) { if (visit(x, y)) break; }
    if (visit(x)) return x;
  }
}

export function outerThrow(xs, ys, visit) {
  for (const x of xs) {
    for (const y of ys(x)) { if (visit(x, y)) break; }
    if (visit(x)) throw x;
  }
}

export function caught(xs, ys, visit) {
  try {
    for (const x of xs) for (const y of ys(x)) {
      if (visit(x, y)) return y;
    }
  } catch (error) { return visit(error); }
}

export async function caughtSuspended(xs, ys, visit) {
  try {
    for (const x of xs) for (const y of ys(x)) {
      if (await visit(x, y)) return y;
    }
  } catch (error) { return visit(error); }
}

export function selectedIterable(xs, ys, visit) {
  let selected;
  if (visit()) selected = xs; else selected = ys();
  for (const x of selected) for (const y of ys(x)) {
    if (visit(x, y)) return y;
  }
}

export function retainedResult(xs, ys, visit) {
  let result;
  for (const x of xs) for (const y of ys(x)) {
    if (visit(x, y)) { result = y; return result; }
  }
  return result;
}
