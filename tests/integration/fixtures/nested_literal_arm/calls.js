export function nested(promptAction, flag, input) {
  try {
    return promptAction.showToast({message: flag ? {a: [input.x]} : {b: [input.y]}, duration: 2000});
  } catch (error) { return input.caught(error); }
}
export function middle(promptAction, flag, input) {
  try {
    return promptAction.showToast({before: input.before, message: flag ? {a: [input.x]} : {b: [input.y]}, after: input.after, duration: 2000});
  } catch (error) { return input.caught(error); }
}
