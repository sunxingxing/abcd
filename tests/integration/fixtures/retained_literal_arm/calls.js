export function retained(promptAction, input) {
  try {
    const condition = input.produce();
    return promptAction.showToast({message: condition ? {a: [input.x]} : {b: [input.y]}, duration: 2000});
  } catch (error) { return input.caught(error); }
}
export function middle(promptAction, input) {
  try {
    const condition = input.produce();
    return promptAction.showToast({before: input.before, message: condition ? {a: [input.x]} : {b: [input.y]}, after: input.after, duration: 2000});
  } catch (error) { return input.caught(error); }
}
export function outside(promptAction, input) {
  const condition = input.produce();
  try {
    return promptAction.showToast({message: condition ? {a: [input.x]} : {b: [input.y]}, duration: 2000});
  } catch (error) { return input.caught(error); }
}
