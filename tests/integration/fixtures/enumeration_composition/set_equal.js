export function sameMembers(a, b, compare) {
  const left = [], right = [];
  a.forEach(value => left.push(value));
  b.forEach(value => right.push(value));
  let equal = true;
  const pairs = [[left, right], [right, left]];
  for (let i = 0; equal && i < pairs.length; i++) {
    const from = pairs[i][0], to = pairs[i][1];
    for (const item of from) {
      let found = false;
      for (let j = 0; !found && j < to.length; j++) found = compare(item, to[j]);
      equal = equal && found;
    }
  }
  return equal;
}
