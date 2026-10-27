export async function singleIteration(xs, ys, visit) {
  for (const x of xs) for (const y of ys(x)) {
    await visit(x, y);
    break;
  }
}
