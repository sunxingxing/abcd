export async function once(xs, rows, visit) {
  for (const x of xs) {
    for (const y of rows(x)) {
      await visit(x, y);
      break;
    }
  }
}
export function onceSync(xs, rows, visit) {
  for (const x of xs) {
    for (const y of rows(x)) {
      visit(x, y);
      break;
    }
  }
}
