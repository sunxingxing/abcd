export function make(n, readers) {
  let saved = this;
  for (let i = 0; i < n; ++i) {
    readers.push(() => saved.id);
    globalThis.writer(value => { saved = value; });
    if (globalThis.stop(i)) break;
  }
  return readers;
}
