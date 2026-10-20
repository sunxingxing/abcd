let Constructor = globalThis.Constructor;
export function loop(receiver, states) {
  try {
    for (const state of states) {
      receiver.method({
        first: globalThis.field('first'),
        made: new Constructor(state.left ? state.a : state.b,
                              state.right ? state.c : state.d),
        selected: state.last ? 'yes' : 'no'
      });
    }
  } catch (error) {
    globalThis.trace.push(['catch', error.constructor.name]);
    return error.constructor.name;
  }
}
