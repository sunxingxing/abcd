export async function bind(mode, log, readers) {
  const self = this;
  if (mode === 'skip') return 'skip';
  try {
    log.push('enter');
    const data = await globalThis.__bind(mode, async () => {
      log.push('read');
      return [data, self.id];
    }, readers, log);
    log.push('initialized');
    if (mode === 'after') throw new RangeError('after');
    return [data, self.id];
  } catch (error) {
    log.push(error.constructor.name);
    return 'caught';
  }
}
