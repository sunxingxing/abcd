export function scan(iterable, visit, mode) {
  let turns = 0;
  outer: while (turns < 2) {
    turns++;
    for (const key of iterable) {
      visit(key);
      if (mode === 'continue') continue outer;
      if (mode === 'break') break outer;
      return key;
    }
  }
  return turns;
}
