// An unguarded local module read (a `var` binding, initialized at module
// entry) inside a closure created by a phased creator: a method or function
// with a try region, an async function and a generator.
export var Mode = { english: 0, chinese: 1 };
export var counter = 0;
export class Controller {
  constructor(sink) { this.sink = sink; }
  register(source) {
    try {
      source.on('change', () => this.sink.push('change'));
    } catch (e) {
      this.sink.push('error:' + e.message);
    }
    source.on('subtype', (id) => {
      counter += 1;
      this.sink.push(id === 'en' ? Mode.english : Mode.chinese);
    });
  }
}
export function guarded(sink) {
  try {
    sink.push('start');
  } finally {
    sink.push('end');
  }
  return () => Mode.chinese + counter;
}
export async function later(sink) {
  await sink.wait();
  return () => Mode.english + counter;
}
export function* steps() {
  yield 1;
  return () => Mode.chinese;
}
