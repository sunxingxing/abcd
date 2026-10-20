export function classify(a, b, hooks) {
  if (hooks.first(a) || hooks.first(b)) {
    const left = hooks.first(a);
    const right = hooks.first(b);
    if (left && right) {
      hooks.equal();
      return 'both';
    }
    if (left || right) {
      hooks.early();
      return 'one';
    }
  }
  hooks.continuing();
  return 'tail';
}
export function dispatch(kind, hooks) {
  if (kind === 0) {
    hooks.left();
  } else if (kind === 1) {
    if (hooks.first(0) || hooks.first(1)) {
      hooks.early();
      return 'early';
    }
    hooks.right();
  } else if (kind === 2) {
    if (hooks.first(0) || hooks.first(1)) {
      hooks.equal();
      return 'equal';
    }
    hooks.right();
  } else {
    hooks.left();
  }
  hooks.continuing();
  return 'tail';
}
export function protectedDispatch(kind, hooks) {
  try {
    if (kind === 0) {
      hooks.left();
    } else if (kind === 1) {
      if (hooks.first(0) || hooks.first(1)) {
        hooks.early();
        return 'early';
      }
      hooks.right();
    } else if (kind === 2) {
      if (hooks.first(0) || hooks.first(1)) {
        hooks.equal();
        return 'equal';
      }
      hooks.right();
    } else {
      hooks.left();
    }
    hooks.continuing();
    return 'tail';
  } catch (error) {
    hooks.caught(error.name);
    return 'caught';
  }
}
