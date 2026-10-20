export function bind(mode, log, readers) {
  try {
    log.push('enter');
    globalThis.__escape(() => data, readers, log);
    const data = globalThis.__initialize(mode, log);
    log.push('initialized');
    if (mode === 'after') throw new RangeError('after');
    return data;
  } catch (error) {
    log.push(error.constructor.name);
    return 'caught';
  }
}
