export function conditional(n, choose, read, consume) {
  for (let i = 0; i < n; ++i) {
    consume({index: i, value: choose(i) ? read(i) : read(-i)});
  }
}
export function surrounding(n, read, consume) {
  let i = 0;
  while (i < n) ++i;
  const value = {index: i, value: read(i) ? 'yes' : 'no'};
  while (i > 0) --i;
  consume(value);
}
export function nested(n, read, consume) {
  for (let i = 0; i < n; ++i) {
    for (let j = 0; j < n; ++j) {
      consume({index: j, value: read(i, j) ? 'yes' : 'no'});
    }
  }
}
