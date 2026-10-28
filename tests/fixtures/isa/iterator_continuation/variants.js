export function suffix(xs, select, read, valid, finish) {
  let fallback = 1;
  for (const x of xs) {
    if (select(x)) {
      const n = read(x);
      return valid(n) && n >= 1 ? n : fallback;
    }
  }
  return finish(fallback);
}

export function arms(xs, select, read, valid) {
  let fallback = 1;
  for (const x of xs) {
    const action = select(x);
    if (action === 1) {
      const n = read(x);
      return valid(n) && n >= 1 ? n : fallback;
    }
    if (action === 2) {
      const n = read(x);
      return valid(n) ? n : fallback;
    }
  }
  return fallback;
}

export function captured(fallback) {
  return function(xs, select, read, valid) {
    for (const x of xs) {
      if (select(x)) {
        const n = read(x);
        return valid(n) && n >= 1 ? n : fallback;
      }
    }
    return fallback;
  };
}

export function throwing(xs, select, read, valid, finish) {
  let fallback = 1;
  for (const x of xs) {
    if (select(x)) {
      const n = read(x);
      return valid(n) ? finish(n) : finish(fallback);
    }
  }
  return fallback;
}

export function caught(xs, select, read, valid, finish) {
  let fallback = 1;
  try {
    for (const x of xs) {
      if (select(x)) {
        const n = read(x);
        return valid(n) && n >= 1 ? n : fallback;
      }
    }
    return finish(fallback);
  } catch (error) {
    return error.name;
  }
}

export function getter(xs, select, read, valid) {
  let fallback = 1;
  for (const x of xs) {
    if (select(x)) {
      const n = read(x);
      return valid(n) ? n.value : fallback;
    }
  }
  return fallback;
}
