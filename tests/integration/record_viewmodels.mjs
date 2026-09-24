import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';

const kConstruct = Symbol();
const kList = Symbol();
const kSliceRead = Symbol();
const kSliceCall = Symbol();
const events = [];

function sliceTarget() {
  const target = {
    get slice() {
      events.push(kSliceRead);
      const invoke = function(...args) {
        assert.equal(this, target);
        events.push(kSliceCall);
        return args;
      };
      Object.defineProperty(invoke, 'call', {
        get() { throw new Error('mutable call lookup'); },
      });
      Object.defineProperty(invoke, 'apply', {
        get() { throw new Error('mutable apply lookup'); },
      });
      return invoke;
    },
  };
  return target;
}

const categoryList = Object.freeze([{id: 1}]);
const welfareList = sliceTarget();
class IconInfoModel {
  constructor() { events.push(kConstruct); }
  getCategoryList() { events.push(kList); return categoryList; }
  getWelfareList() { events.push(kList); return welfareList; }
}

const sectionProducts = sliceTarget();
const flashProducts = sliceTarget();
class SectionModel {
  constructor() { events.push(kConstruct); }
  getSectionProductList() { events.push(kList); return sectionProducts; }
  getFlashSaleProductList() { events.push(kList); return flashProducts; }
}

async function loadRecord(path, specifier, exportName, imported) {
  const dependency = new vm.SyntheticModule([exportName], function() {
    this.setExport(exportName, imported);
  });
  const recovered = new vm.SourceTextModule(readFileSync(path, 'utf8'));
  await recovered.link((requested) => {
    assert.equal(requested, specifier);
    return dependency;
  });
  await recovered.evaluate();
  return recovered.namespace;
}

const icon = await loadRecord(
  process.argv[2],
  '@normalized:N&&&shopping/src/main/ets/model/IconInfoModel&1.0.0',
  'IconInfoModel', IconInfoModel);
assert.equal(icon.IconInfoViewModel.name, 'IconInfoViewModel');
assert.equal(icon.IconInfoViewModel.length, 0);
const iconView = new icon.IconInfoViewModel();
assert.ok(Object.hasOwn(iconView, 'iconInfoModel'));
assert.ok(iconView.iconInfoModel instanceof IconInfoModel);
assert.deepEqual(events, [kConstruct]);
assert.equal(iconView.getCategoryList(), categoryList);
assert.deepEqual(events, [kConstruct, kList]);
assert.deepEqual(iconView.getWelfareList(2), [0, 2]);
assert.deepEqual(events, [kConstruct, kList, kList, kSliceRead, kSliceCall]);
assert.deepEqual(iconView.getWelfareList(), [0, undefined]);
assert.deepEqual(events.slice(-3), [kList, kSliceRead, kSliceCall]);
assert.equal(icon.IconInfoViewModel.prototype.getWelfareList.length, 1);

events.length = 0;
const section = await loadRecord(
  process.argv[3],
  '@normalized:N&&&shopping/src/main/ets/model/SectionModel&1.0.0',
  'SectionModel', SectionModel);
assert.equal(section.SectionViewModel.name, 'SectionViewModel');
assert.equal(section.SectionViewModel.length, 0);
const sectionView = new section.SectionViewModel();
assert.ok(Object.hasOwn(sectionView, 'model'));
assert.ok(sectionView.model instanceof SectionModel);
assert.deepEqual(events, [kConstruct]);
assert.deepEqual(sectionView.getSectionProductList(1), [0, 1]);
assert.deepEqual(sectionView.getFlashSaleProductList(3), [0, 3]);
assert.deepEqual(events, [kConstruct, kList, kSliceRead, kSliceCall,
                          kList, kSliceRead, kSliceCall]);
assert.equal(section.SectionViewModel.prototype.getSectionProductList.length, 1);
assert.equal(section.SectionViewModel.prototype.getFlashSaleProductList.length, 1);
