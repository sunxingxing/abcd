export async function unsupported(xs, visit) {
  for await (const x of xs) {
    await visit(x);
    break;
  }
}
