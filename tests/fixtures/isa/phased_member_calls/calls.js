export function run(log, obj, tag) {
  try {
    log.info(tag, "x" + obj.cameras + ", length: " + obj.cameras?.length + "");
  } catch (e) { return e; }
}
export function template(log, obj, tag) {
  try {
    return log.info(tag, `x${obj.cameras}, length: ${obj.cameras?.length}`);
  } catch (e) { return e; }
}
export function base(container, obj, tag) {
  try {
    return container.log.info(tag, `x${obj.cameras}, length: ${obj.cameras?.length}`);
  } catch (e) { return e; }
}
export async function before(log, obj, tag, wait) {
  try {
    const result = log.info(tag, `x${obj.cameras}, length: ${obj.cameras?.length}`);
    await wait(1);
    return result;
  } catch (e) { return e; }
}
export async function after(log, obj, tag, wait) {
  try {
    await wait(1);
    return log.info(tag, `x${obj.cameras}, length: ${obj.cameras?.length}`);
  } catch (e) { return e; }
}
export function nested(log, obj, tag) {
  try {
    return log.info(tag, `x${obj.cameras}, next: ${obj.cameras?.next(tag)}`);
  } catch (e) { return e; }
}
export function optionalCall(log, obj, tag, cb) {
  try {
    return log.info(tag, `x${obj.cameras}, next: ${cb?.(tag)}`);
  } catch (e) { return e; }
}
export function choices(log, obj, tag, test, arg) {
  try {
    return log.info(tag, "x" + (test() ? arg(1) : arg(2)) + ", next: " +
      (test() ? arg(3) : arg(4)) + "");
  } catch (e) { return e; }
}
