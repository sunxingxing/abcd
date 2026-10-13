import Base from 'base';
import initialize from 'initialize';
export default class Derived extends Base {
  x = initialize('x', this);
  y = initialize('y', this);
}
