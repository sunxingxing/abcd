export async function breakAndPattern(xs, groups, stop, visit) {
  for (const x of xs) {
    if (stop(x)) break;
    try {
      for (const [a, b] of groups(x)) {
        if (await visit(a, b)) break;
      }
    } catch (error) {
      visit(error);
    }
  }
}
