// Independent source oracle; the engine consumes only switch_fallthrough.abc.
export function dispatch(message, hooks) {
  switch (message.kind) {
    case 'first': hooks.first(message.value); break;
    case 'second': if (message.value) hooks.second(message.value);
    case 'third': hooks.third(message.value); break;
    default: hooks.other(message.value);
  }
  return hooks.end();
}
export function three(kind, hooks) {
  switch (kind) {
    case 0: hooks.first();
    case 1: hooks.second();
    case 2: hooks.third(); break;
    default: hooks.other();
  }
  return hooks.end();
}
export function middle(kind, hooks) {
  switch (kind) {
    case 0: hooks.first();
    default: hooks.other();
    case 2: hooks.third(); break;
    case 3: hooks.second();
  }
  return hooks.end();
}
export function empty(kind, hooks) {
  switch (kind) {
    case 0:
    case 1: hooks.first();
    case 2: { let x = hooks.value; hooks.second(x); } break;
    default: hooks.other();
  }
}
export function exits(kind, hooks) {
  switch (kind) {
    case 0: if (hooks.value) break; hooks.first();
    case 1: return hooks.second();
    default: hooks.other();
  }
  return hooks.end();
}
export function loop(count, hooks) {
  let result = 0;
  let i = 0;
  while (i < count) {
    ++i;
    const kind = hooks.kind(i);
    switch (kind) {
      case 0: hooks.first();
      case 1: hooks.second(); break;
      case 2: continue;
      default: result++;
    }
    hooks.third();
  }
  return result;
}
export function ordered(message, hooks) {
  switch (message.kind) {
    case hooks.test(0): hooks.first();
    default: hooks.other();
    case hooks.test(2): hooks.third(); break;
    case hooks.test(3): hooks.second();
  }
  return hooks.end();
}
