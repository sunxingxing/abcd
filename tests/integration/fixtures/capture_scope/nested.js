export async function bind(mode, log, readers) {
  if (mode === 'skip') return 'skip';
  try {
    log.push('enter');
    if (mode !== 'other') {
      const data = await globalThis.__bind(mode, async () => {
        log.push('read');
        return data;
      }, readers, log);
      log.push('initialized');
      return data;
    }
    return 'other';
  } catch (error) {
    log.push(error.constructor.name);
    return 'caught';
  }
}
