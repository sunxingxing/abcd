export function tdz(receiver, early) {
  const invoke = () => receiver.method({value: data});
  if (early) return invoke();
  let data = globalThis.field('initialize');
  return invoke();
}
