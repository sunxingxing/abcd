export function query(xs, log) {
  try {
    for (const item of xs) {
      log.push(item);
      if (item.stop) break;
    }
  } catch (error) {
    log.push(error.code);
    return error.message;
  }
  return "done";
}
