export function scan(iterable, visit) {
 let sum=0;
 const update=function(value){sum=sum+value;return sum;};
 for(const key of iterable){visit(key,update);}
 return update(0);
}
