import {x} from 'm'; export class A{ n:number;constructor(){'use sendable';this.n=1;} m(){return x;}} export function f(){class B{constructor(){'use sendable';} m(){return B;}}return B;}
