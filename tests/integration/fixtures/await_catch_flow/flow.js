// Independently authored input. The harness supplies all effects and failures.
export async function checked(ops, log) {
  let preserved = ops.seed;
  try {
    log.push('enter');
    const value = await ops.wait();
    log.push('fulfilled', value);
    return value;
  } catch (error) {
    try {
      const method = ops.recover;
      if (ops.branch) return method.call(ops, error, preserved, log);
      log.push('fallback', preserved);
      return preserved;
    } catch (nested) {
      log.push('nested', nested.constructor.name, preserved);
      return -1;
    }
  }
}
export async function ignored(ops, log) {
  try { await ops.wait(); return 1; }
  catch { log.push('ignored'); return ops.result; }
}
export async function rethrow(ops, log) {
  const preserved = 13;
  try { return await ops.wait(); }
  catch (error) { log.push('rethrow', preserved); throw error; }
}
export async function cleanup(ops, log) {
  try { return await ops.wait(); }
  catch (error) { log.push('caught', error.constructor.name); return 9; }
  finally { log.push('finally'); }
}
