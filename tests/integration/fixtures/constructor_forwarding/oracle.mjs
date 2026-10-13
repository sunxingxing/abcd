// Independent source-contract oracle. Assignment is intentional: native class
// fields use [[DefineOwnProperty]] and would fail the inherited-setter oracle.
import Base from 'base';
import initialize from 'initialize';
export default class Derived extends Base {
  constructor(...args) {
    super(...args);
    this.x = initialize('x', this);
    this.y = initialize('y', this);
  }
}
