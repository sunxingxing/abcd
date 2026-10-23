// for-of loop bodies with captured block-scoped bindings: es2abc protects the
// body's per-iteration environment with a `poplexenv; throw` cleanup handler
// that forwards into the iterator-close handler. Also sequential loops.
export function captured(items, sink) {
  for (const item of items) {
    let label = item.label;
    sink.keep(() => label);
    sink.push(label);
  }
}
export function sequential(first, second, sink) {
  for (const value of first) {
    sink.push(value);
  }
  for (const item of second) {
    let label = item.label;
    sink.keep(() => label + '!');
  }
}
export function nested(items, sink) {
  for (const item of items) {
    sink.push(item.label);
    {
      let inner = item.label + '?';
      sink.keep(() => inner);
    }
  }
}
