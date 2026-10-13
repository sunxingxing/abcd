export function early(outer, stop, choice, hooks) {
  if (outer) {
    hooks.enter();
    if (stop) {
      if (choice) {
        hooks.left();
      } else {
        hooks.right();
      }
      hooks.terminal();
      return 'early';
    }
    hooks.continuing();
  }
  hooks.tail();
  return 'tail';
}
export function protectedEarly(outer, stop, choice, hooks) {
  try {
    if (outer) {
      hooks.enter();
      if (stop) {
        if (choice) {
          hooks.left();
        } else {
          hooks.right();
        }
        hooks.terminal();
        return 'early';
      }
      hooks.continuing();
    }
    hooks.tail();
    return 'tail';
  } catch (error) {
    hooks.caught(error.name);
    return 'caught';
  }
}
export function independent(choice, nested, hooks) {
  if (choice) {
    if (nested) hooks.left();
    hooks.terminal();
    return 'left';
  } else {
    if (nested) hooks.right();
    hooks.terminal();
    return 'right';
  }
}
