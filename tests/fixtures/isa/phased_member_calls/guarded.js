// Unproved capture/guard composition must retain its typed Reference refusal.
const capturedTag = 'T';
export function run(log, obj, ignored, fn) {
  let result = [];
  try {
    result = fn();
    log.info(capturedTag, `x${obj.cameras}, length: ${obj.cameras?.length}`);
  } catch (e) {
    log.error(capturedTag, e.code);
  }
  return result;
}
