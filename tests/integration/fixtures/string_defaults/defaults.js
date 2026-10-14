export class Logger {
  constructor(prefix = 'LOG', domain = 65280) {
    this.prefix = prefix;
    this.domain = domain;
  }
  format() { return this.prefix + ':' + this.domain; }
}
export function sequence(prefix = 'first\n😀', domain = 65280, suffix = 'last') {
  return prefix + ':' + domain + ':' + suffix;
}
export function leading(required, prefix = 'tail', domain = 65280) {
  return required + ':' + prefix + ':' + domain;
}
export const arrow = (prefix = 'arrow', domain = 65280) => prefix + ':' + domain;
