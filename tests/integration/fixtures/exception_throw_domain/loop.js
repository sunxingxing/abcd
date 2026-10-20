export function query(xs, log) {
  for (const item of xs) {
    log.push(item);
    break;
  }
  return "done";
}
