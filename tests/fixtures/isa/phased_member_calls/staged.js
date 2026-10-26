// The live result needs a pre-throw catch snapshot throughout the logger call.
// Uniform snapshots are shared before the complete native expression.
export function run(log, obj, tag, fn) {
  let result = [];
  try {
    result = fn();
    log.info(tag, `x${obj.cameras}, length: ${obj.cameras?.length}`);
  } catch (e) {
    log.error(tag, e.code);
  }
  return result;
}
