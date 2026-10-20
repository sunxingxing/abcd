let utility = globalThis.utility;
let Constructor = globalThis.Constructor;
export function run(receiver, state) {
  try {
    return receiver.method({
      first: globalThis.field('first'),
      guarded: utility.pick(globalThis.field('pick')),
      made: new Constructor(state.left ? state.a : state.b,
                            state.right ? state.c : state.d,
                            globalThis.field('tail')),
      selected: state.last ? 'yes' : 'no'
    });
  } catch (error) {
    globalThis.trace.push(['catch', error.constructor.name]);
    return error.constructor.name;
  }
}
export function conditional(receiver, state) {
  try {
    return receiver.method({
      before: globalThis.field('before'),
      message: state.left ? utility.pick(state.a) : utility.pick(state.b),
      after: globalThis.field('after')
    });
  } catch (error) {
    globalThis.trace.push(['catch', error.constructor.name]);
    return error.constructor.name;
  }
}
