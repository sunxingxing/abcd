import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';

const events = [];
const constants = {
  CATEGORY_INFO_LIST: ['category-a', 'category-b'],
  CATEGORY_ICON_LIST: ['a.png', 'b.png'],
  WELFARE_PRICE_LIST: ['price-a', 'price-b'],
  WELFARE_ICON_LIST: ['welfare-a.png', 'welfare-b.png'],
  SECTION_PRODUCT_IMAGES: ['section-a.png', 'section-b.png'],
  SECTION_PRODUCTS_TITLES: [['title-a'], ['title-b']],
  SECTION_PRODUCTS_PRICES: ['10', '20'],
  SECTION_PRODUCTS_FEATURES: [['feature-a'], ['feature-b']],
  SECTION_PRODUCTS_COMMENTS: 'comments',
  SECTION_PRODUCTS_INFO: ['info-a', 'info-b'],
  FLASH_SALE_PRODUCT_IMAGES: ['flash-a.png', 'flash-b.png'],
  FLASH_SALE_PRODUCT_TITLES: ['flash-title-a', 'flash-title-b'],
  FLASH_SALE_PRODUCT_PRICES: ['5', '6'],
  FLASH_SALE_PRODUCT_FEATURES: ['fast-a', 'fast-b'],
  FLASH_SALE_PRODUCT_INFO: ['flash-info-a', 'flash-info-b'],
  FLASH_SALE_PRODUCT_ORIGINAL_PRICES: ['8', undefined],
};
const tracked = new Proxy(constants, {
  get(target, key, receiver) {
    events.push(key);
    return Reflect.get(target, key, receiver);
  },
});
const moduleSpecifier =
  '@normalized:N&&&shopping/src/main/ets/constants/HomeConstants&1.0.0';
async function loadRecord(path) {
  const dependency = new vm.SyntheticModule(['HomeConstants'], function() {
    this.setExport('HomeConstants', tracked);
  });
  const recovered = new vm.SourceTextModule(readFileSync(path, 'utf8'));
  await recovered.link((specifier) => {
    assert.equal(specifier, moduleSpecifier);
    return dependency;
  });
  await recovered.evaluate();
  return recovered.namespace;
}

const icon = await loadRecord(process.argv[2]);
assert.equal(icon.IconInfoModel.name, 'IconInfoModel');
assert.equal(icon.IconInfo.name, 'IconInfo');
const iconModel = new icon.IconInfoModel();
assert.deepEqual(events, [
  'CATEGORY_INFO_LIST', 'CATEGORY_ICON_LIST', 'CATEGORY_ICON_LIST',
  'WELFARE_PRICE_LIST', 'WELFARE_ICON_LIST', 'WELFARE_ICON_LIST',
]);
assert.deepEqual(iconModel.getCategoryList().map(item =>
  [item.getInfo(), item.getIcon()]),
  [['category-a', 'a.png'], ['category-b', 'b.png']]);
assert.deepEqual(iconModel.getWelfareList().map(item =>
  [item.getInfo(), item.getIcon()]),
  [['price-a', 'welfare-a.png'], ['price-b', 'welfare-b.png']]);
assert.ok(iconModel.getCategoryList().every(item => item instanceof icon.IconInfo));
const firstCategories = iconModel.getCategoryList();
iconModel.initData();
assert.equal(iconModel.getCategoryList(), firstCategories);
assert.equal(firstCategories.length, 4);

events.length = 0;
const section = await loadRecord(process.argv[3]);
assert.equal(section.SectionModel.name, 'SectionModel');
const sectionModel = new section.SectionModel();
assert.deepEqual(sectionModel.getSectionProductList().map(item =>
  [item.getProductImg(), item.getTitle(), item.getPrice(),
   item.getFeature(), item.getComments(), item.getInfo()]), [
  ['section-a.png', ['title-a'], '10', ['feature-a'], 'comments', 'info-a'],
  ['section-b.png', ['title-b'], '20', ['feature-b'], 'comments', 'info-b'],
]);
const flash = sectionModel.getFlashSaleProductList();
assert.equal(flash.length, 2);
assert.ok(flash.every(item => item instanceof section.SectionProduct));
// The paired source itself passes six values to a seven-parameter constructor.
assert.equal(flash[0].getComments(), 'flash-info-a');
assert.equal(flash[0].getInfo(), '8');
assert.equal(flash[0].getOriginalPrice(), undefined);
assert.equal(Object.hasOwn(flash[0], 'originalPrice'), false);
assert.equal(flash[1].getInfo(), undefined);
assert.equal(flash[1].getOriginalPrice(), undefined);
assert.equal(Object.hasOwn(flash[1], 'originalPrice'), false);
assert.deepEqual(events, [
  'SECTION_PRODUCT_IMAGES',
  ...Array(2).fill([
    'SECTION_PRODUCTS_TITLES', 'SECTION_PRODUCTS_PRICES',
    'SECTION_PRODUCTS_FEATURES', 'SECTION_PRODUCTS_COMMENTS',
    'SECTION_PRODUCTS_INFO']).flat(),
  'FLASH_SALE_PRODUCT_IMAGES',
  ...Array(2).fill([
    'FLASH_SALE_PRODUCT_TITLES', 'FLASH_SALE_PRODUCT_PRICES',
    'FLASH_SALE_PRODUCT_FEATURES', 'FLASH_SALE_PRODUCT_INFO',
    'FLASH_SALE_PRODUCT_ORIGINAL_PRICES']).flat(),
]);
