export let value = 1;
export const bump = () => { value = value + 1; };

globalThis.lazyEvents.push('dep');
