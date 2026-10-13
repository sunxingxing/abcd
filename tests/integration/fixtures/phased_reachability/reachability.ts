export function caught(input) {
  try {
    return input.run();
  } catch (error) {
    return input.recover(error);
  }
}

export function finalized(input) {
  let result;
  try {
    result = input.run();
  } finally {
    input.finish();
  }
  return result;
}
