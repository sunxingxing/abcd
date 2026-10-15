import {identity} from './dependency.js';
export async function success(value, trace) {
  "use concurrent";
  trace.push('enter');
  const result = await identity(value);
  trace.push('resume');
  return result;
}
export async function before(trace) {
  "use concurrent";
  trace.push('before');
  throw new TypeError('before');
}
export async function after(value, trace) {
  "use concurrent";
  await value;
  trace.push('after');
  throw new RangeError('after');
}
export async function rejected(value, trace) {
  "use concurrent";
  trace.push('await');
  return await value;
}
export async function returned(value, trace) {
  "use concurrent";
  trace.push('return');
  return value;
}
export async function handled(value, trace) {
  "use concurrent";
  try {
    await value;
  } catch {
    trace.push('caught');
  }
}
