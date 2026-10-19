export function caught(n, read, consume, failed) {
  for (let i = 0; i < n; ++i) {
    try { consume({index: i, value: read(i) ? 'yes' : 'no'}); }
    catch (e) { failed(e); }
  }
}
