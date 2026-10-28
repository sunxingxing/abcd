export function nestedReturn(xs, rows, visit) {
  for (const x of xs) {
    for (const y of rows(x)) {
      return visit(x, y);
    }
  }
  return 'empty';
}
