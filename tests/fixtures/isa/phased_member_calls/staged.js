// The live result needs a pre-throw catch snapshot throughout the logger call.
// Staging inside an expression is a separate, still-refused emission obligation.
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
