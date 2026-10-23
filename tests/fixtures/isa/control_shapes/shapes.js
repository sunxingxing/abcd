// Early returns around try regions and nested conditionals.
export function guardThenTry(s) {
  if (s.a && s.b) {
    return 'guard';
  }
  try {
    return s.run();
  } catch (e) {
    return 'caught:' + e.message;
  }
}
export function elseIfReturns(s) {
  if (s.a) {
    if (s.b) {
      return 'ab';
    }
  } else if (s.c) {
    if (s.d) {
      return 'cd';
    }
  }
  return s.run();
}
export function returnInsideTry(s) {
  try {
    if (s.a) {
      if (s.b) {
        return 'ab';
      }
    }
    s.run();
  } catch (e) {
    s.log('caught:' + e.message);
  }
  return 'end';
}
export function chainInsideTry(s) {
  try {
    s.log('start');
    if (s.a) {
      s.log('a');
      if (s.b) {
        return 'ab';
      }
    } else if (s.c) {
      s.log('c');
      if (s.d) {
        return 'cd';
      }
    }
    s.run();
  } catch (e) {
    s.log('caught:' + e.message);
  }
  return 'end';
}
