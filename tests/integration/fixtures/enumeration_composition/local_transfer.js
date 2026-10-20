export function scan(groups, visit) {
  const values = [];
  for (let i = 0; i < groups.length; ++i) {
    for (const value of groups[i]) {
      for (let j = 0; j < 2; ++j) {
        if (visit(value, j)) break;
        if (j === 0) continue;
      }
      values.push(value);
    }
  }
  return values;
}
