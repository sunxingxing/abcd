export async function header(n, keep) {
  for (let i = 0; i < n; i++) keep(() => i);
}
