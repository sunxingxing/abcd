export async function finalized(n, keep, wait) {
  for (let i = 0; i < n; i++) {
    let value = new Object(i);
    try { keep(() => value); } finally { keep(() => value); }
  }
  return await wait;
}
