// Inner loops whose exits reach a shared continue corridor that es2abc
// splits per incoming edge (`jmp` to the for-update block).
export function toSuffix(expression, isOperator, isGrouping, isPrioritized) {
  const operatorStack = [];
  const suffix = [];
  let top;
  for (let index = 0, size = expression.length; index < size; ++index) {
    const element = expression[index];
    if (element === '(') {
      operatorStack.push(element);
      continue;
    }
    if (element === ')') {
      if (operatorStack.length) {
        let operator = operatorStack.pop();
        while (operator !== '(' && operator !== undefined) {
          suffix.push(operator);
          operator = operatorStack.pop();
        }
      }
      continue;
    }
    if (isOperator(element)) {
      if (!operatorStack.length) {
        operatorStack.push(element);
      } else {
        top = operatorStack[operatorStack.length - 1];
        let pop = operatorStack.pop();
        if (!isGrouping(top) && !isPrioritized(element, top) && pop !== undefined) {
          while (operatorStack.length) {
            suffix.push(pop);
            operatorStack.pop();
          }
        }
        operatorStack.push(element);
      }
      continue;
    }
    suffix.push(element);
  }
  while (operatorStack.length) {
    let pop = operatorStack.pop();
    if (pop !== undefined) {
      suffix.push(pop);
    }
  }
  return suffix;
}
export function f(xs, s, o) {
  for (let i = 0; i < xs; ++i) {
    if (xs[i]) {
      if (s.length) {
        while (s.a && s.b) {
          o.push(1);
        }
      }
      continue;
    }
    o.push(2);
  }
}
