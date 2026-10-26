export async function body(n, keep, wait) {
  for (let i = 0; i < n; i++) {
    let value = new Object(i);
    let count = i;
    keep(() => value, delta => count += delta, () => count);
  }
  return await wait;
}
export async function continued(n, keep, wait) {
  for (let i = 0; i < n; i++) {
    let value = new Object(i);
    keep(() => value);
    if (i === 1) continue;
    keep(() => value);
  }
  return await wait;
}
export async function stopped(n, keep, wait) {
  for (let i = 0; i < n; i++) {
    let value = new Object(i);
    keep(() => value);
    if (i === 2) break;
  }
  return await wait;
}
export async function thrown(n, keep, wait) {
  for (let i = 0; i < n; i++) {
    let value = new Object(i);
    keep(() => value);
    if (i === 2) throw value;
  }
  return await wait;
}
export async function suspended(n, keep, wait) {
  for (let i = 0; i < n; i++) {
    let value = new Object(i);
    keep(() => value);
    await wait;
    keep(() => value);
  }
}
export async function nested(n, keep, wait) {
  for (let i = 0; i < n; i++) {
    let value = new Object(i);
    { let j = 0; while (j < 2) { let inner = j++; keep(() => value, () => inner); } }
  }
  return await wait;
}
export async function factory(n, keep, create, wait) {
  for (let i = 0; i < n; i++) {
    let value = create(i);
    let count = i;
    keep(() => value, delta => count += delta, () => count);
  }
  return await wait;
}
