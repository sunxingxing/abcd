export async function destructured(xs, rows, visit) {
  try {
    for (const x of xs) {
      for (const [a, b] of rows(x)) {
        await visit(a, b);
        break;
      }
    }
    return 'done';
  } catch (error) {
    return error;
  }
}
export function allReturn(xs, rows, visit) {
  for (const x of xs) {
    return visit(x);
  }
  return 'empty';
}
export function allThrow(xs, rows, visit) {
  for (const x of xs) {
    throw visit(x);
  }
  return 'empty';
}
export async function mixed(xs, rows, visit) {
  for (const x of xs) {
    if (await visit(x)) {
      break;
    }
    return x;
  }
  return 'done';
}
export function nestedTerminal(xs, rows, visit) {
  for (const x of xs) {
    for (const y of rows(x)) {
      throw visit(x, y);
    }
    break;
  }
}
export function returnInWhile(xs, rows, visit) {
  let i = 0;
  while (i < 2) {
    for (const y of rows(i)) {
      return visit(i, y);
    }
    i++;
  }
  return 'empty';
}
export async function beforeAfter(xs, rows, visit) {
  await visit('before');
  for (const x of xs) {
    for (const y of rows(x)) {
      await visit(x, y);
      break;
    }
  }
  return await visit('after');
}
