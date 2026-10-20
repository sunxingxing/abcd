export function query(read, log) {
  try {
    try {
      return read();
    } catch (error) {
      log.push("inner");
      throw error;
    }
  } catch (error) {
    log.push(error.code);
    return error.message;
  }
}
