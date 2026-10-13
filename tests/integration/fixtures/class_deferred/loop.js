export function probe() {
  let values = [];
  for (let i = 0; i < 2; ++i) {
    class Local { static read() { return Local; } }
    values.push(Local.read);
  }
  return values[0]() !== values[1]();
}
